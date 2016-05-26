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

#pragma once

#include "gvcf_options.hh"
#include "starling_common/starling_base_shared.hh"



struct starling_options : public starling_base_options
{
    typedef starling_options base_t;

    starling_options()
    {
        // set command-line defaults for starling only:
        gvcf.out_file = "-";
        bsnp_ssd_no_mismatch = 0.35;
        bsnp_ssd_one_mismatch = 0.6;
        max_win_mismatch = 2;
        max_win_mismatch_flank_size = 20;
        is_min_vexp = true;
        min_vexp = 0.25;

        // TODO double-check with MK:
        ///upstream_oligo_size = 10;
    }

    bool
    is_bsnp_diploid() const override
    {
        return is_ploidy_prior;
    }

    bool
    is_all_sites() const override
    {
        return true;
    }

    bool
    is_compute_germline_scoring_metrics() const override
    {
        return (isReportEVSFeatures || (! germline_variant_scoring_model_name.empty()));
    }

    /// germline scoring models
    std::string germline_variant_scoring_models_filename;

    /// Which scoring model should we use?
    std::string germline_variant_scoring_model_name;

    // Apply codon phasing:
    bool do_codon_phasing = false;

    // Size of the window we are phasing in, default is codon range (=3)
    int phasing_window = 3;

    gvcf_options gvcf;
};


// data deterministically derived from the input options:
//
struct starling_deriv_options : public starling_base_deriv_options
{
    typedef starling_base_deriv_options base_t;

    starling_deriv_options(
        const starling_options& opt,
        const reference_contig_segment& ref)
        : base_t(opt,ref),
          gvcf(opt.gvcf,opt.bam_seq_name)
    {}

    gvcf_deriv_options gvcf;
};

