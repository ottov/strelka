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

#include "GetSequenceErrorCounts.hh"
#include "GetSequenceErrorCountsInfo.hh"
#include "GetSequenceErrorCountsRun.hh"
#include "SequenceErrorCountsOptionsParser.hh"



namespace
{
const prog_info& pinfo(GetSequenceErrorCountsInfo::get());
}



void
GetSequenceErrorCounts::
runInternal(int argc, char* argv[]) const
{
    SequenceErrorCountsOptions opt;

    for (int i(0); i<argc; ++i)
    {
        if (i) opt.cmdline += ' ';
        opt.cmdline += argv[i];
    }

    po::variables_map vm;
    try
    {
        po::options_description visible(getSequenceErrorCountsOptionsParser(opt));
        po::parsed_options parsed(po::command_line_parser(argc,argv).options(visible).run());
        po::store(parsed,vm);
        po::notify(vm);
    }
    catch (const boost::program_options::error& e)
    {
        pinfo.usage(e.what());
    }

    if ((argc==1) || vm.count("help"))
    {
        pinfo.usage();
    }

    finalizeSequenceErrorCountsOptions(pinfo,vm,opt);

    getSequenceErrorCountsRun(pinfo,opt);
}
