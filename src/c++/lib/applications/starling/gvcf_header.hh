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

///
/// \author Chris Saunders
///

#pragma once

#include "ScoringModelManager.hh"
#include "starling_shared.hh"
#include "blt_util/chrom_depth_map.hh"

#include <iosfwd>


void
finish_gvcf_header(
    const starling_options& opt,
    const gvcf_deriv_options& dopt,
    const cdmap_t& chrom_depth,
    const std::vector<std::string>& sampleNames,
    std::ostream& os);
