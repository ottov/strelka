//
// Strelka - Small Variant Caller
// Copyright (c) 2009-2018 Illumina, Inc.
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

#include "starling_continuous_variant_caller.hh"
#include "blt_util/math_util.hh"
#include "blt_util/qscore.hh"

#include <boost/math/special_functions/gamma.hpp>
#include <boost/math/distributions/binomial.hpp>



/// Get a p-value for the hypothesis that 'allele' was generated as sequencing error under a simple Poisson error model
///
/// \param[in] alleleObservationCount Observation count of the allele in question
/// \param[in] totalObservationCount Observation count of all alleles
/// \param[in] expectedObservationQscore Approximate that all observations have the same error probability given by
///                                       this value (expressed as a phred-scaled quality score)
///
/// \return The above-described p-value
static
double
getAlleleSequencingErrorProb(
    const unsigned alleleObservationCount,
    const unsigned totalObservationCount,
    const int expectedObservationQscore)
{
    if (alleleObservationCount == 0)
        return 1.0;

    const double expectedObservationErrorRate(qphred_to_error_prob(expectedObservationQscore));

    // Expected error count assuming no variant allele is present (poisson \lambda parameter)
    const double expectedObservationErrorCount(totalObservationCount * expectedObservationErrorRate);

    // Return the probability that an allele observation count of 'alleleObservationCount' or higher would be
    // generated by sequencing error.
    //
    // Note that the regularized incomplete gamma function (gamma_p) is being used here to compute the complement
    // Poisson CDF value gamma_p(k, \lambda), reflecting the probability of k or more observations.
    //
    return (boost::math::gamma_p(alleleObservationCount, expectedObservationErrorCount));
}



int
starling_continuous_variant_caller::
getAlleleSequencingErrorQscore(
    const unsigned alleleObservationCount,
    const unsigned totalObservationCount,
    const int expectedObservationQscore,
    const int maxQScore)
{
    /// TODO: enable this assertion to be safely added in production
    // assert(alleleObservationCount <= totalObservationCount);

    // When alleleObservationCount is an alternate allele (and the only alternate allele), alleleErrorProb
    // is related to the probability that the locus is non-variant
    double alleleErrorProb = getAlleleSequencingErrorProb(
                                 alleleObservationCount, totalObservationCount, expectedObservationQscore);

    if (alleleErrorProb <= 0) return maxQScore;
    return std::min(maxQScore, error_prob_to_qphred(alleleErrorProb));
}



static
double
binomialLogDensity(
    unsigned trials,
    unsigned successes,
    double successProb)
{
    using namespace boost::math;

    assert((successProb >= 0.) and (successProb <= 1.));
    assert(successes <= trials);

    if (trials==0) return 0;
    return std::log(pdf(binomial(trials, successProb), successes));
}



double
starling_continuous_variant_caller::
strandBias(
    unsigned fwdAlt,
    unsigned revAlt,
    unsigned fwdOther,
    unsigned revOther)
{
    const unsigned fwdTotal(fwdAlt+fwdOther);
    const unsigned revTotal(revAlt+revOther);
    const unsigned total(fwdTotal+revTotal);
    if (total==0) return 0;

    const double fwdAltFreq(safeFrac(fwdAlt,fwdTotal));
    const double revAltFreq(safeFrac(revAlt,revTotal));
    const double altFreq(safeFrac(fwdAlt + revAlt, total));

    static const double errorRate(0.005);

    const double fwdLnp(binomialLogDensity( fwdTotal, fwdAlt, fwdAltFreq) + binomialLogDensity( revTotal, revAlt, errorRate));
    const double revLnp(binomialLogDensity( fwdTotal, fwdAlt, errorRate) + binomialLogDensity( revTotal, revAlt, revAltFreq));
    const double lnp(binomialLogDensity( fwdTotal, fwdAlt, altFreq) + binomialLogDensity( revTotal, revAlt, altFreq));


    return std::max(fwdLnp, revLnp) - lnp;
}
