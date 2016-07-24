// -*- mode: c++; indent-tabs-mode: nil; -*-
//
// Strelka - Small Variant Caller
// Copyright (c) 2009-2016 Illumina, Inc.
//
// This program is free software: you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation, either version 3 of the License, or
// at your option) any later version.
//
// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License for more details.
//
// You should have received a copy of the GNU General Public License
// along with this program.  If not, see <http://www.gnu.org/licenses/>.
//
//

///
/// \author Chris Saunders
///


#include "gvcf_locus_info.hh"
#include "blt_util/math_util.hh"
#include "common/Exceptions.hh"

#include "boost/math/distributions/binomial.hpp"
#include "boost/math/distributions.hpp"
#include "rnaVariantEmpiricalScoringFeatures.hh"

#include <iostream>
#include <map>
#include <sstream>
#include <typeinfo>



void
LocusFilterKeeper::
write(std::ostream& os) const
{
    if (filters.none())
    {
        os << "PASS";
        return;
    }

    bool is_sep(false);
    for (unsigned i(0); i<GERMLINE_VARIANT_VCF_FILTERS::SIZE; ++i)
    {
        if (! filters.test(i)) continue;

        if (is_sep)
        {
            os << ";";
        }
        else
        {
            is_sep=true;
        }
        os << GERMLINE_VARIANT_VCF_FILTERS::get_label(i);
    }
}



pos_t
GermlineDiploidIndelLocusInfo::
end() const
{
    pos_t result = 0;
    for (auto& x : altAlleles)
        result = std::max(result, x._indelKey.right_pos());
    return result;
}



static
void
add_cigar_to_ploidy(
    const ALIGNPATH::path_t& apath,
    std::vector<unsigned>& ploidy)
{
    using namespace ALIGNPATH;
    int offset(-1);
    for (const auto& ps : apath)
    {
        if (is_segment_align_match(ps.type))
        {
            for (unsigned j(0); j<ps.length; ++j)
            {
                if (offset>=0) ploidy[offset]++;
                offset++;
            }
        }
        else if (ps.type==DELETE)
        {
            offset+=ps.length;
        }
    }
}



void
GermlineDiploidIndelLocusInfo::
add_overlap(
    const reference_contig_segment& ref,
    GermlineDiploidIndelLocusInfo& overlap)
{
    assert(altAlleles.size() == 1);
    assert(overlap.altAlleles.size() == 1);

    auto& firstAllele(first());
    auto& overlapAllele(overlap.first());

    // there's going to be 1 (possibly empty) fill range in front of one haplotype
    // and one possibly empty fill range on the back of one haplotype
    std::string leading_seq,trailing_seq;
    auto indel_end_pos=std::max(overlapAllele._indelKey.right_pos(),firstAllele._indelKey.right_pos());

    const pos_t indel_begin_pos(pos-1);

    // add shared information (to the first indel only)
    // TODO: Reevaluate this. Since we're merging, we can do this on output
    // make extended vcf ref seq:
    std::string tmp;
    ref.get_substring(indel_begin_pos,(indel_end_pos-indel_begin_pos),tmp);
    firstAllele._indelReportInfo.vcf_ref_seq = tmp;

    ploidy.resize(indel_end_pos-pos,0);

    auto munge_indel = [&] (GermlineDiploidIndelLocusInfo& ii)
    {
        auto& this_call(ii.first());
        // extend leading sequence start back 1 for vcf compat, and end back 1 to concat with vcf_indel_seq
        ref.get_substring(indel_begin_pos,(ii.pos-indel_begin_pos)-1,leading_seq);
        const unsigned trail_len(indel_end_pos-this_call._indelKey.right_pos());
        ref.get_substring(indel_end_pos-trail_len,trail_len,trailing_seq);

        this_call._indelReportInfo.vcf_indel_seq = leading_seq + this_call._indelReportInfo.vcf_indel_seq + trailing_seq;

        this_call.set_hap_cigar(leading_seq.size()+1,
                                trailing_seq.size());

        // add to the ploidy object:
        add_cigar_to_ploidy(this_call.cigar,ploidy);
    };
    munge_indel(*this);
    munge_indel(overlap);

    // we only combine pairs of simple het indels on different haplotpyes, so this assertion must hold:
    for (const unsigned pl : ploidy)
    {
        assert(pl<2);
    }

    //reduce qual and gt to the lowest of the set:
    firstAllele._dindel.indel_qphred = std::min(firstAllele._dindel.indel_qphred, overlap.first()._dindel.indel_qphred);
    firstAllele._dindel.max_gt_qphred = std::min(firstAllele._dindel.max_gt_qphred, overlap.first()._dindel.max_gt_qphred);


    // combine filter flags from overlapping loci:
    filters.merge(overlap.filters);

    // combine EVS values. Since the "unset" value is -1, this complex logic is necessary
    if (empiricalVariantScore <0)
        empiricalVariantScore = overlap.empiricalVariantScore;
    else if (overlap.empiricalVariantScore >= 0)
        empiricalVariantScore = std::min(empiricalVariantScore, overlap.empiricalVariantScore);
    firstAllele.gqx = std::min(firstAllele.gqx, overlap.first().gqx);
    firstAllele.gq = std::min(firstAllele.gq, overlap.first().gq);

    altAlleles.push_back(overlap.first());
}



void
GermlineDiploidIndelLocusInfo::
getPloidyError(
    const unsigned offset) const
{
    using namespace illumina::common;

    std::ostringstream oss;
    oss << "ERROR: get_ploidy offset '" << offset << "' exceeds ploidy region size '" << ploidy.size() << "'\n";
    BOOST_THROW_EXCEPTION(LogicException(oss.str()));
}



void
GermlineDiploidSiteLocusInfo::
computeEmpiricalScoringFeatures(
    const bool isRNA,
    const bool isUniformDepthExpected,
    const bool isComputeDevelopmentFeatures,
    const double chromDepth)
{
    const double chromDepthFactor(safeFrac(1, chromDepth));

    const double filteredLocusDepth(n_used_calls);
    const double locusDepth(mapqCount);

    const double filteredLocusDepthFactor(safeFrac(1, filteredLocusDepth));
    const double locusDepthFactor(safeFrac(1, locusDepth));

    // get the alt base id (choose second in case of an alt het....)
    unsigned altBase(N_BASE);
    for (unsigned b(0); b < N_BASE; ++b)
    {
        if (b == dgt.ref_gt) continue;
        if (DIGT::expect2(b, smod.max_gt))
        {
            altBase = b;
        }
    }
    assert(altBase != N_BASE);

    const unsigned r0 = alleleObservationCounts(dgt.ref_gt);
    const unsigned r1 = alleleObservationCounts(altBase);

    const double mapqZeroFraction(safeFrac(mapqZeroCount, mapqCount));

    const double locusUsedDepthFraction(filteredLocusDepth * locusDepthFactor);


    if (isRNA)
    {
        double genotype(2.0);
        if (is_het() or is_hetalt()) genotype = 1.0;
        EVSFeatures.set(RNA_SNV_SCORING_FEATURES::GT, (genotype));

        EVSFeatures.set(RNA_SNV_SCORING_FEATURES::QUAL, (dgt.genome.snp_qphred * chromDepthFactor));
        EVSFeatures.set(RNA_SNV_SCORING_FEATURES::F_DP, (n_used_calls * chromDepthFactor));
        EVSFeatures.set(RNA_SNV_SCORING_FEATURES::F_DPF, (n_unused_calls * chromDepthFactor));
        EVSFeatures.set(RNA_SNV_SCORING_FEATURES::F_GQ, (smod.gq * chromDepthFactor));
        EVSFeatures.set(RNA_SNV_SCORING_FEATURES::F_GQX, (smod.gqx * chromDepthFactor));

        EVSFeatures.set(RNA_SNV_SCORING_FEATURES::I_AvgBaseQ, (avgBaseQ));
        EVSFeatures.set(RNA_SNV_SCORING_FEATURES::I_AvgPos, (rawPos));

        EVSFeatures.set(RNA_SNV_SCORING_FEATURES::I_BaseQRankSum, (BaseQRankSum));
        EVSFeatures.set(RNA_SNV_SCORING_FEATURES::I_ReadPosRankSum, (ReadPosRankSum));

        EVSFeatures.set(RNA_SNV_SCORING_FEATURES::I_SNVHPOL, (hpol));
        EVSFeatures.set(RNA_SNV_SCORING_FEATURES::I_SNVSB, (smod.strand_bias));

        EVSFeatures.set(RNA_SNV_SCORING_FEATURES::AD0, (r0 * chromDepthFactor));
        EVSFeatures.set(RNA_SNV_SCORING_FEATURES::AD1, (r1 * chromDepthFactor));

        EVSFeatures.set(RNA_SNV_SCORING_FEATURES::ADR, safeFrac(r0, (r0 + r1)));

        // compute any experimental features not currently used in production
        //
        if (isComputeDevelopmentFeatures)
        {
            EVSDevelopmentFeatures.set(RNA_SNV_SCORING_DEVELOPMENT_FEATURES::I_MQ, (mapqRMS));
            EVSDevelopmentFeatures.set(RNA_SNV_SCORING_DEVELOPMENT_FEATURES::I_MQRankSum, (MQRankSum));

            EVSDevelopmentFeatures.set(RNA_SNV_SCORING_DEVELOPMENT_FEATURES::mapqZeroFraction, mapqZeroFraction);

            EVSDevelopmentFeatures.set(RNA_SNV_SCORING_DEVELOPMENT_FEATURES::F_DP_NORM, locusUsedDepthFraction);

            EVSDevelopmentFeatures.set(RNA_SNV_SCORING_DEVELOPMENT_FEATURES::QUAL_NORM,
                                          (dgt.genome.snp_qphred * filteredLocusDepthFactor));
            EVSDevelopmentFeatures.set(RNA_SNV_SCORING_DEVELOPMENT_FEATURES::F_GQX_NORM,
                                          (smod.gqx * filteredLocusDepthFactor));
            EVSDevelopmentFeatures.set(RNA_SNV_SCORING_DEVELOPMENT_FEATURES::F_GQ_NORM,
                                          (smod.gq * filteredLocusDepthFactor));

            EVSDevelopmentFeatures.set(RNA_SNV_SCORING_DEVELOPMENT_FEATURES::AD0_NORM,
                                          (r0 * filteredLocusDepthFactor));
            EVSDevelopmentFeatures.set(RNA_SNV_SCORING_DEVELOPMENT_FEATURES::AD1_NORM,
                                          (r1 * filteredLocusDepthFactor));

            EVSDevelopmentFeatures.set(RNA_SNV_SCORING_DEVELOPMENT_FEATURES::QUAL_EXACT,
                                          (dgt.genome.snp_qphred));
            EVSDevelopmentFeatures.set(RNA_SNV_SCORING_DEVELOPMENT_FEATURES::F_GQX_EXACT, (smod.gqx));
            EVSDevelopmentFeatures.set(RNA_SNV_SCORING_DEVELOPMENT_FEATURES::F_GQ_EXACT, (smod.gq));
        }
    }
    else
    {
        {
            double genotype(0);
            if (is_hetalt()) genotype = 2;
            else if (not is_het()) genotype = 1;
            EVSFeatures.set(GERMLINE_SNV_SCORING_FEATURES::GENO, genotype);
        }

        EVSFeatures.set(GERMLINE_SNV_SCORING_FEATURES::I_MQ, (mapqRMS));
        EVSFeatures.set(GERMLINE_SNV_SCORING_FEATURES::I_SNVHPOL, (hpol));
        EVSFeatures.set(GERMLINE_SNV_SCORING_FEATURES::I_SNVSB, (smod.strand_bias));
        EVSFeatures.set(GERMLINE_SNV_SCORING_FEATURES::I_MQRankSum, (MQRankSum));
        EVSFeatures.set(GERMLINE_SNV_SCORING_FEATURES::I_ReadPosRankSum, (ReadPosRankSum));

        // how surprising is the depth relative to expect? This is the only value will be modified for exome/targeted runs
        /// TODO: convert this to pvalue based on Poisson distro?
        double relativeLocusDepth(1.);
        if (isUniformDepthExpected)
        {
            relativeLocusDepth = (locusDepth * chromDepthFactor);
        }

        EVSFeatures.set(GERMLINE_SNV_SCORING_FEATURES::TDP_NORM, relativeLocusDepth);

        // how noisy is the locus?
        EVSFeatures.set(GERMLINE_SNV_SCORING_FEATURES::F_DP_NORM, locusUsedDepthFraction);

        EVSFeatures.set(GERMLINE_SNV_SCORING_FEATURES::F_GQX_EXACT, (smod.gqx));

        // compute any experimental features not currently used in production
        //
        if (isComputeDevelopmentFeatures)
        {

            // BaseQRankSum
            EVSDevelopmentFeatures.set(GERMLINE_SNV_SCORING_DEVELOPMENT_FEATURES::I_BaseQRankSum, (BaseQRankSum));

            // allele bias metrics
            {
                const double allelebiaslower = cdf(boost::math::binomial(r0 + r1, 0.5), r0);
                const double allelebiasupper = cdf(boost::math::binomial(r0 + r1, 0.5), r1);

                // +1e-30 to avoid log(0) in extreme cases
                EVSDevelopmentFeatures.set(GERMLINE_SNV_SCORING_DEVELOPMENT_FEATURES::ABlower, (-log(allelebiaslower + 1.e-30)));
                EVSDevelopmentFeatures.set(GERMLINE_SNV_SCORING_DEVELOPMENT_FEATURES::AB,
                                              (-log(std::min(1., 2. * std::min(allelebiaslower, allelebiasupper)) + 1.e-30)));
            }

            //The average baseQ of the position of alt allele
            EVSDevelopmentFeatures.set(GERMLINE_SNV_SCORING_DEVELOPMENT_FEATURES::I_RawBaseQ, (avgBaseQ));

            //the average position value within a read of alt allele
            EVSDevelopmentFeatures.set(GERMLINE_SNV_SCORING_DEVELOPMENT_FEATURES::I_RawPos, (rawPos));

            // hom unrelable are the read mappings near this locus?
            EVSDevelopmentFeatures.set(GERMLINE_SNV_SCORING_DEVELOPMENT_FEATURES::mapqZeroFraction, mapqZeroFraction);

            // renormalized features intended to replace the corresponding production feature
            //
            EVSDevelopmentFeatures.set(GERMLINE_SNV_SCORING_DEVELOPMENT_FEATURES::QUAL_NORM,
                                          (dgt.genome.snp_qphred * filteredLocusDepthFactor));
            EVSDevelopmentFeatures.set(GERMLINE_SNV_SCORING_DEVELOPMENT_FEATURES::F_GQX_NORM,
                                          (smod.gqx * filteredLocusDepthFactor));
            EVSDevelopmentFeatures.set(GERMLINE_SNV_SCORING_DEVELOPMENT_FEATURES::F_GQ_NORM,
                                          (smod.gq * filteredLocusDepthFactor));

            EVSDevelopmentFeatures.set(GERMLINE_SNV_SCORING_DEVELOPMENT_FEATURES::AD0_NORM,
                                          (r0 * filteredLocusDepthFactor));

            EVSDevelopmentFeatures.set(GERMLINE_SNV_SCORING_DEVELOPMENT_FEATURES::QUAL_EXACT,
                                          (dgt.genome.snp_qphred));

            EVSDevelopmentFeatures.set(GERMLINE_SNV_SCORING_DEVELOPMENT_FEATURES::F_GQ_EXACT, (smod.gq));

            EVSDevelopmentFeatures.set(GERMLINE_SNV_SCORING_DEVELOPMENT_FEATURES::AD1_NORM,
                                          (r1 * filteredLocusDepthFactor));
        }
    }
}



std::ostream&
operator<<(std::ostream& os,
           const GermlineDiploidSiteLocusInfo& si)
{
    os << "pos: " << (si.pos+1) << " " << si.get_gt();
    return os;
}



void
GermlineDiploidIndelLocusInfo::
dump(std::ostream& os) const
{
    os << "digt_indel_info\n";
    os << "nCalls: " << altAlleles.size() << " isOverlap: " << _is_overlap << "\n";
    os << "ploidy: ";
    for (const unsigned pl : ploidy)
    {
        os << " " << pl;
    }
    os << "\n";
    os << "Calls:\n";
    for (const auto& cl : altAlleles)
    {
        os << cl << "\n";
    }
}
