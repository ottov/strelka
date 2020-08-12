#!/usr/bin/env python
#
# Strelka - Small Variant Caller
# Copyright (c) 2009-2018 Illumina, Inc.
#
# This program is free software: you can redistribute it and/or modify
# it under the terms of the GNU General Public License as published by
# the Free Software Foundation, either version 3 of the License, or
# at your option) any later version.
#
# This program is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
# GNU General Public License for more details.
#
# You should have received a copy of the GNU General Public License
# along with this program.  If not, see <http://www.gnu.org/licenses/>.
#
#

"""
Learn empirical variant scoring model from (set of) labeled feature files
"""

__author__ = "Peter Krusche <pkrusche@illumina.com>"


import os
import sys
import json

import pandas

scriptDir = os.path.abspath(os.path.dirname(__file__))
workflowDir = os.path.abspath(os.path.join(scriptDir, "../lib"))

sys.path.append(workflowDir)


import evs
import evs.tools
import evs.features
import random

def parseArgs():
    import argparse

    parser = argparse.ArgumentParser(description="evs learning script",
                                     formatter_class=argparse.ArgumentDefaultsHelpFormatter)

    parser.add_argument("inputs", nargs="+",
                        help="Labeled feature CSV file (argument may be specified more than once")

    modelNames=evs.EVSModel.names()
    parser.add_argument("-m", "--model", choices=modelNames, required=True,
                        help="Which model to use (options are: %s)" % str(modelNames))

    parser.add_argument("--features",
                        choices=list(evs.features.FeatureSet.sets.keys()),
                        required=True,
                        help="Training features. Either a feature-table name or a comma-separated list of feature names."
                             " e.g. QSS_NT,T_DP_RATE")

    parser.add_argument("-p", "--parameter-file", dest="parameters",
                        help="Specify additional parameters for the learning algorithm as a JSON file")

    parser.add_argument("-o", "--output", required=True,
                        help="Output file name")

    parser.add_argument("--balance_overall", default=False, action="store_true",
                        help="Balance the number of samples from each label in the overall (combined) dataset")

    parser.add_argument("--balance_per_sample", default=False, action="store_true",
                        help="Balance the number of samples from each label separately in every input file")

    parser.add_argument("--sample-input", dest="sample_input", default=0, type=int,
                        help="Number of rows to subsample from each input data file")

    parser.add_argument("--plots", default=False, action="store_true",
                        help="Make plots.")

    args = parser.parse_args()

    def checkFile(filename, label) :
        if not os.path.isfile(filename) :
            raise Exception("Can't find input %s file: '%s'" % (label,filename))

    def checkOptionalFile(filename, label) :
        if filename is None : return
        checkFile(filename,label)

    if len(args.inputs) == 0 :
        raise Exception("No input file(s) given")

    for inputFile in args.inputs :
        checkFile(inputFile,"features CSV")

    checkOptionalFile(args.parameters, "training model parameter")

    return args



def getDataSet(inputs, sample_input, balance_per_sample) :

    import random
    datasets = []
    admixWeight = 1
    nnWeight = 1

    for inputFile in inputs:
        inputFile = os.path.abspath(inputFile)
        print(("Reading '%s'" % (inputFile)))
        df = pandas.read_csv(inputFile)
        # Remove false negatives before any subsampling:
        df = df[df["tag"] != "FN"]

        if sample_input:
            p_rows = min(df.shape[0], sample_input)
            p_rows_selected = random.sample(df.index, p_rows)
            df = pandas.DataFrame(df.ix[p_rows_selected])

        if balance_per_sample:
            tps = df[df["tag"] == "TP"]
            fps = df[df["tag"] == "FP"]
            print(("TP: %d FP: %d" % (tps.shape[0], fps.shape[0])))
            if tps.shape[0] < fps.shape[0]:
                rows_selected = random.sample(fps.index, tps.shape[0])
                fps = pandas.DataFrame(fps.ix[rows_selected])
            elif fps.shape[0] < tps.shape[0]:
                rows_selected = random.sample(tps.index, fps.shape[0])
                tps = pandas.DataFrame(tps.ix[rows_selected])
            print(("Downsampled to TP: %d FP: %d" % (tps.shape[0], fps.shape[0])))
            df = pandas.concat([tps, fps])

        df["weight"] = 1
        if "Admix" in inputFile:
            df["weight"] = admixWeight
            print(("Admixture: setting weight to %f" % admixWeight))
        if "NN" in inputFile:
            df["weight"] = nnWeight
            print(("Normal-normal: setting weight to %f" % nnWeight))
        datasets.append(df)

    if len(datasets) > 1:
        dataset = pandas.concat(datasets)
    else:
        dataset = datasets[0]

    return pandas.DataFrame(dataset)



def main():
    args = parseArgs()

    try:
        fset = evs.features.FeatureSet.make(args.features)
        features = fset.trainingfeatures()
    except:
        features = args.features.split(",")

    pars = {}
    if args.parameters :
        pars = json.load(open(args.parameters))
        print(("Using custom learning parameters: %s" % str(pars)))
    else :
        print("Using default learning parameters.")

    model = evs.EVSModel.createNew(args.model)

    dataset = getDataSet(args.inputs, args.sample_input, args.balance_per_sample)

    if (not args.balance_overall) or args.balance_per_sample:
        tpdata = dataset[dataset["tag"] == "TP"]
        fpdata = dataset[dataset["tag"] == "FP"]
    else:
        tpdata2 = dataset[dataset["tag"] == "TP"]
        fpdata2 = dataset[dataset["tag"] == "FP"]
        nrows = min(tpdata2.shape[0], fpdata2.shape[0])
        tp_rows_selected = random.sample(tpdata2.index, nrows)
        fp_rows_selected = random.sample(fpdata2.index, nrows)
        tpdata = tpdata2.ix[tp_rows_selected]
        fpdata = fpdata2.ix[fp_rows_selected]

    model.train(tpdata, fpdata, features, **pars)
    model.save(args.output)

    if args.plots and hasattr(model, "plots"):
        model.plots(args.output + ".plots", features)
    elif args.plots:
        print(("No plots created, this is not supported by %s" % args.model))


if __name__ == '__main__':
    main()
