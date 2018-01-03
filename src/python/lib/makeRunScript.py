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
This provides a function to auto-generate a workflow run script.
"""

import os, sys

from configureUtil import pickleConfigSections



def makeRunScript(scriptFile, workflowModulePath, workflowClassName, primaryConfigSection, configSections, pythonBin=None) :
    """
    This function generates the python workflow runscript

    The auto-generated python script presents the user with options to
    run and/or continue their workflow, and reads all workflow
    configuration info from an ini file.

    scriptFile -- file name of the runscript to create
    workflowModulePath -- the python module containing the workflow class
    workflowClassName -- the workflow class name
    primaryConfigSection -- the section used to create the primary workflow parameter object
    configSections -- a hash or hashes representing all configuration info
    @param pythonBin: optionally specify a custom python interpreter for the script she-bang
    """

    assert os.path.isdir(os.path.dirname(scriptFile))
    assert os.path.isfile(workflowModulePath)

    workflowModulePath=os.path.abspath(workflowModulePath)
    workflowModuleDir=os.path.dirname(workflowModulePath)
    workflowModuleName=os.path.basename(workflowModulePath)
    pyExt=".py"
    if workflowModuleName.endswith(pyExt) :
        workflowModuleName=workflowModuleName[:-len(pyExt)]

    # dump inisections to a file
    pickleConfigFile=scriptFile+".config.pickle"
    pickleConfigSections(pickleConfigFile,configSections)

    sfp=open(scriptFile,"w")

    if pythonBin is None :
        pythonBin="/usr/bin/env python2"

    sfp.write(runScript1 % (pythonBin, " ".join(sys.argv),workflowModuleDir,workflowModuleName,workflowClassName))

    sfp.write('\n')
    sfp.write(runScript2)
    sfp.write('\n')
    sfp.write(runScript3)
    sfp.write('\n')

    sfp.write('main(r"%s","%s",%s)\n' % (pickleConfigFile, primaryConfigSection, workflowClassName))
    sfp.write('\n')
    sfp.close()
    os.chmod(scriptFile,0755)



runScript1="""#!%s
# Workflow run script auto-generated by command: '%s'
#

import os, sys

if sys.version_info >= (3,0):
    import platform
    raise Exception("Strelka does not currently support python3 (version %%s detected)" %% (platform.python_version()))

if sys.version_info < (2,6):
    import platform
    raise Exception("Strelka requires python2 version 2.6+ (version %%s detected)" %% (platform.python_version()))

scriptDir=os.path.abspath(os.path.dirname(__file__))
sys.path.append(r'%s')

from %s import %s

"""


runScript2="""
def get_run_options(workflowClassName) :

    from optparse import OptionGroup, SUPPRESS_HELP

    from configBuildTimeInfo import workflowVersion
    from configureUtil import EpilogOptionParser
    from estimateHardware import EstException, getNodeHyperthreadCoreCount, getNodeMemMb

    sgeDefaultCores=workflowClassName.runModeDefaultCores('sge')

    epilog=\"\"\"Note this script can be re-run to continue the workflow run in case of interruption.
Also note that dryRun option has limited utility when task definition depends on upstream task
results -- in this case the dry run will not cover the full 'live' run task set.\"\"\"

    parser = EpilogOptionParser(description="Version: %s" % (workflowVersion), epilog=epilog, version=workflowVersion)


    parser.add_option("-m", "--mode", type="string",dest="mode",
                      help="select run mode (local|sge)")
    parser.add_option("-q", "--queue", type="string",dest="queue",
                      help="specify scheduler queue name")
    parser.add_option("-j", "--jobs", type="string",dest="jobs",
                  help="number of jobs, must be an integer or 'unlimited' (default: Estimate total cores on this node for local mode, %s for sge mode)" % (sgeDefaultCores))
    parser.add_option("-g","--memGb", type="string",dest="memGb",
                  help="gigabytes of memory available to run workflow -- only meaningful in local mode, must be an integer (default: Estimate the total memory for this node for local mode, 'unlimited' for sge mode)")
    parser.add_option("-d","--dryRun", dest="isDryRun",action="store_true",default=False,
                      help="dryRun workflow code without actually running command-tasks")
    parser.add_option("--quiet", dest="isQuiet",action="store_true",default=False,
                      help="Don't write any log output to stderr (but still write to workspace/pyflow.data/logs/pyflow_log.txt)")

    def isLocalSmtp() :
        import smtplib
        try :
            smtplib.SMTP('localhost')
        except :
            return False
        return True

    isEmail = isLocalSmtp()
    emailHelp = SUPPRESS_HELP
    if isEmail :
        emailHelp="send email notification of job completion status to this address (may be provided multiple times for more than one email address)"

    parser.add_option("-e","--mailTo", type="string",dest="mailTo",action="append",help=emailHelp)

    debug_group = OptionGroup(parser,"development debug options")
    debug_group.add_option("--rescore", dest="isRescore",action="store_true",default=False,
                          help="Reset task list to re-run hypothesis generation and scoring without resetting graph generation.")

    parser.add_option_group(debug_group)

    ext_group = OptionGroup(parser,"extended portability options (should not be needed by most users)")
    ext_group.add_option("--maxTaskRuntime", type="string", metavar="hh:mm:ss",
                      help="Specify scheduler max runtime per task, argument is provided to the 'h_rt' resource limit if using SGE (no default)")

    parser.add_option_group(ext_group)

    (options,args) = parser.parse_args()

    if not isEmail : options.mailTo = None

    if len(args) :
        parser.print_help()
        sys.exit(2)

    if options.mode is None :
        parser.print_help()
        sys.exit(2)
    elif options.mode not in ["local","sge"] :
        parser.error("Invalid mode. Available modes are: local, sge")

    if options.jobs is None :
        if options.mode == "sge" :
            options.jobs = sgeDefaultCores
        else :
            try :
                options.jobs = getNodeHyperthreadCoreCount()
            except EstException:
                parser.error("Failed to estimate cores on this node. Please provide job count argument (-j).")
    if options.jobs != "unlimited" :
        options.jobs=int(options.jobs)
        if options.jobs <= 0 :
            parser.error("Jobs must be 'unlimited' or an integer greater than 1")

    # note that the user sees gigs, but we set megs
    if options.memGb is None :
        if options.mode == "sge" :
            options.memMb = "unlimited"
        else :
            try :
                options.memMb = getNodeMemMb()
            except EstException:
                parser.error("Failed to estimate available memory on this node. Please provide available gigabyte argument (-g).")
    elif options.memGb != "unlimited" :
        options.memGb=int(options.memGb)
        if options.memGb <= 0 :
            parser.error("memGb must be 'unlimited' or an integer greater than 1")
        options.memMb = 1024*options.memGb
    else :
        options.memMb = options.memGb

    options.schedulerArgList=[]
    if options.queue is not None :
        options.schedulerArgList.extend(["-q",options.queue])
    if options.maxTaskRuntime is not None :
        options.schedulerArgList.extend(["-l","h_rt="+options.maxTaskRuntime])

    options.resetTasks=[]
    if options.isRescore :
        options.resetTasks.append("makeHyGenDir")

    return options
"""


runScript3="""
def main(pickleConfigFile, primaryConfigSection, workflowClassName) :
    from configureUtil import getConfigWithPrimaryOptions

    runOptions=get_run_options(workflowClassName)
    flowOptions,configSections=getConfigWithPrimaryOptions(pickleConfigFile,primaryConfigSection)

    # new logs and marker files to assist automated workflow monitoring:
    warningpath=os.path.join(flowOptions.runDir,"workflow.warning.log.txt")
    errorpath=os.path.join(flowOptions.runDir,"workflow.error.log.txt")
    exitpath=os.path.join(flowOptions.runDir,"workflow.exitcode.txt")

    # the exit path should only exist once the workflow completes:
    if os.path.exists(exitpath) :
        if not os.path.isfile(exitpath) :
            raise Exception("Unexpected filesystem item: '%s'" % (exitpath))
        os.unlink(exitpath)

    wflow = workflowClassName(flowOptions)

    retval=1
    try:
        retval=wflow.run(mode=runOptions.mode,
                         nCores=runOptions.jobs,
                         memMb=runOptions.memMb,
                         dataDirRoot=flowOptions.workDir,
                         mailTo=runOptions.mailTo,
                         isContinue="Auto",
                         isForceContinue=True,
                         isDryRun=runOptions.isDryRun,
                         isQuiet=runOptions.isQuiet,
                         schedulerArgList=runOptions.schedulerArgList,
                         resetTasks=runOptions.resetTasks,
                         successMsg=wflow.getSuccessMessage(),
                         warningLogFile=warningpath,
                         errorLogFile=errorpath)
    finally:
        exitfp=open(exitpath,"w")
        exitfp.write("%i\\n" % (retval))
        exitfp.close()

    sys.exit(retval)
"""
