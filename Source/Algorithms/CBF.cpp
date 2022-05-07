#include "../batchScheduler.h"

Metrics runCBF(std::vector<Node> nodeList, std::vector<Job> jobList, timestamp startTime)
{
    std::ofstream outputfile("CBFOutput.txt", std::ios::trunc);
    // int reservedJobNum = NO_JOB_RESERVING;
    std::vector<Job> jobQueue;
    std::vector<Job> runningJobs;
    std::vector<Job> finalJobList;
    std::vector<Job> reservingJobs;
    // timestamp shadowTime(startTime);
    timestamp currentTime(startTime);
    print("Running the CBF scheduling algorithm.\n");
    outputfile << "Running the CBF scheduling algorithm.\n";
    int simIteration = 0;
    jobList = verifyJobs(jobList, nodeList);
    Metrics cbfMetrics = Metrics("CBF");
    cbfMetrics.totalJobsRun = jobList.size();

    while (!simulationFinished(jobList, jobQueue, runningJobs))
    {

        print("CBF scheduling iteration number: ", simIteration, "\n", "ReservingJobs: ");
        printReservedJobs(reservingJobs);
        // outputfile << "CBF scheduling iteration number: " << simIteration << "\n";
        //  First check if any jobs are ready to be added to the queue:
        if (jobList.size())
        {
            for (std::vector<Job>::iterator currentJobIter = std::prev(jobList.end()); currentJobIter != std::prev(jobList.begin()); --currentJobIter)
            {
                Job currentJob = *currentJobIter;
                // If the job is ready to be submitted right now, put it in the queue and remove it from the joblist:
                if (currentJob.submitTime == currentTime)
                {
                    outputfile << "Adding job num: " << currentJob.jobNum << " to queue\n";
                    currentJob.jobStatus = QUEUED;
                    jobQueue.push_back(currentJob);
                    jobList.erase(currentJobIter);
                }
            }
        }
        // Sort jobs based on the submit times for CBF:
        // std::cout << "Current time: " << currentTime << " queue before sort.\n";
        // printJobs(jobQueue);
        std::sort(jobQueue.begin(), jobQueue.end(), [](const auto &lhs, const auto &rhs)
                  { return lhs.submitTime > rhs.submitTime; });
        // Reversing items in container to switch the order to descending:
        std::reverse(jobQueue.begin(), jobQueue.end());

        print("Sorted job queue:\n");
        outputfile << "Sorted job queue: ";
        printJobs(jobQueue);

        if (runningJobs.size())
        {
            // Check if any running jobs are finished:
            for (std::vector<Job>::iterator runningJob = std::prev(runningJobs.end()); runningJob != std::prev(runningJobs.begin()); --runningJob)
            {
                // if ((*runningJob).jobNum == 2)
                outputfile << "Time taken by job " << (*runningJob).jobNum << ": " << (currentTime - (*runningJob).startTime) << "\n";

                if (currentTime == ((*runningJob).startTime + (*runningJob).trueRunTime))
                {
                    // print("Job: ", (*runningJob).jobNum, " finished running on node: ", (*runningJob).nodeID, "\n");
                    outputfile << "Job: " << (*runningJob).jobNum << " finished running on node: " << (*runningJob).nodeID << "\n";
                    // Reset resources
                    (*runningJob).stopTime = currentTime;

                    finalJobList.push_back((*runningJob));

                    // Reset node resources: (CPU cores and memory allocated)
                    nodeList.at((*runningJob).nodeID).coresAllocated -= (*runningJob).requestedCPUs;
                    nodeList.at((*runningJob).nodeID).memoryAllocated -= (*runningJob).requestedMemory;
                    runningJobs.erase(runningJob);
                }

                // Kill jobs if they go over their requested time.
                else if ((currentTime - (*runningJob).startTime) > ((*runningJob).requestedRunTime))
                {
                    outputfile << "Job: " << (*runningJob).jobNum << " killed due to time limit on node: " << (*runningJob).nodeID << "\n";
                    // Reset resources
                    (*runningJob).stopTime = currentTime;

                    finalJobList.push_back((*runningJob));

                    // Reset node resources: (CPU cores and memory allocated)
                    nodeList.at((*runningJob).nodeID).coresAllocated -= (*runningJob).requestedCPUs;
                    nodeList.at((*runningJob).nodeID).memoryAllocated -= (*runningJob).requestedMemory;
                    runningJobs.erase(runningJob);
                }
            }
        }

        std::vector<Job> waitingList;
        // Finally, start jobs in the queue as resources permit:
        for (std::vector<Job>::iterator waitingJob = jobQueue.begin(); waitingJob != jobQueue.end(); ++waitingJob)
        {
            int selectedNodeID = checkNodeResources((*waitingJob), nodeList);
            bool backfilling = false;
            // bool canFinishInTime = canFinishBeforeShadowCBF.(shadowTime, (*waitingJob).requestedRunTime, currentTime);
            // bool jobsAreReserving = jobsReserving(waitingList);
            //  If we have a node that is available, assign the waiting job to run on it:
            if (selectedNodeID > -1)
            {
                Job selectedJob = (*waitingJob);
                // If we have a job waiting on the shadow time, then we need to see if this one fits before running.
                if (reservingJobs.size() && reservingJobs.at(0).jobNum != selectedJob.jobNum)
                {
                    // Takes a list of reserving jobs and uses the shadow time of the first job in the queue on our node to determine if it can run
                    bool canFinishInTime = canFinishBeforeShadowCBF(runningJobs, (*waitingJob).requestedRunTime, selectedNodeID, currentTime);

                    if (!canFinishInTime)
                    {
                        print("=====================\nJob ", selectedJob.jobNum, " Cannot finish in time as another job is reserving a runtime slot!\n=====================\n");
                        outputfile << "=====================\nJob " << selectedJob.jobNum << " Cannot finish in time while job: as another job is reserving a runtime slot!\n=====================\n";
                        continue; // to the next job that might fit.
                    }
                    backfilling = true;
                    outputfile << "\n=====================\nJob " << selectedJob.jobNum << " Can be backfilled and finish in time!\n=====================\n";
                }
                // We are the first job and/or we can fit without waiting and are the first. Update the shadow time if we extend past the current one/
                // else if (!canFinishInTime)
                // {
                //    shadowTime = currentTime + selectedJob.requestedRunTime;
                // }

                selectedJob.startTime = currentTime;
                selectedJob.waitTime = currentTime - selectedJob.submitTime;
                selectedJob.nodeID = selectedNodeID;

                // TODO: VERIFY THIS WORKS AS INTENDED TO REMOVE JOB FROM RESERVINGLIST
                // reservingJobs.erase(std::remove(reservingJobs.begin(), reservingJobs.end(), (*runningJob)), reservingJobs.end());

                // We can no longer reserve a slot if, ya know, we are already using one and running.
                if (std::find_if(reservingJobs.begin(), reservingJobs.end(), [&](Job const &job)
                                 { return job.jobNum == selectedJob.jobNum; }) != reservingJobs.end())
                {
                    print("Selected Job: ", selectedJob.jobNum, "Started removing itself from the queue for reserved jobs!");
                    // reservingJobs.erase(waitingJob);
                    for (std::vector<Job>::iterator reservingJob = reservingJobs.begin(); reservingJob != reservingJobs.end(); ++reservingJob)
                    {
                        if ((*reservingJob).jobNum == selectedJob.jobNum)
                        {
                            reservingJobs.erase(reservingJob);
                            break;
                        }
                    }
                    print("Selected Job: ", selectedJob.jobNum, "Removed itself from the queue for reserved jobs!");
                }

                // Notify the next job waiting for our node, if it exists.
                if (reservingJobs.size() && !backfilling)
                {
                    for (std::vector<Job>::iterator reservingJob = reservingJobs.begin(); reservingJob != reservingJobs.end(); ++reservingJob)
                    {
                        if ((*reservingJob).nodeID == selectedJob.nodeID)
                        {
                            (*reservingJob).shadowTime = (selectedJob.startTime + selectedJob.requestedRunTime);
                            print("Job: ", selectedJob.jobNum, " updated shadow time of job: ", (*reservingJob).jobNum);
                            break;
                        }
                    }
                }
                //  Add this job's waiting time to the total time:
                cbfMetrics.totalWaitSum += selectedJob.waitTime;
                cbfMetrics.longestWait = (cbfMetrics.longestWait < selectedJob.waitTime) ? selectedJob.waitTime : cbfMetrics.longestWait;

                // Collect stretch metrics:
                selectedJob.stretch = (double)(selectedJob.waitTime + selectedJob.trueRunTime) / (double)selectedJob.trueRunTime;
                cbfMetrics.totalStretch += selectedJob.stretch;
                cbfMetrics.maxStretch = (cbfMetrics.maxStretch < selectedJob.stretch) ? selectedJob.stretch : cbfMetrics.maxStretch;

                // Compute turnaround time and add to total:
                selectedJob.turnAroundTime = (selectedJob.startTime + selectedJob.trueRunTime) - selectedJob.submitTime;
                cbfMetrics.totalturnAroundTime += selectedJob.turnAroundTime;
                cbfMetrics.maxTurnAroundTime = (cbfMetrics.maxTurnAroundTime < selectedJob.turnAroundTime) ? selectedJob.turnAroundTime : cbfMetrics.maxTurnAroundTime;

                // Allocate resources for the waiting job:
                nodeList.at(selectedNodeID).coresAllocated += selectedJob.requestedCPUs;
                nodeList.at(selectedNodeID).memoryAllocated += selectedJob.requestedMemory;
                selectedJob.jobStatus = RUNNING;
                selectedJob.nodeID = selectedNodeID;
                runningJobs.push_back(selectedJob);
                print("Running job ", selectedJob.jobNum, " with a submit time of: ", selectedJob.submitTime, " seconds on node: ", selectedNodeID, "\n");
                print("Running job ", selectedJob.jobNum, " with a start time of: ", selectedJob.startTime, " seconds\n");
                print("Running job ", selectedJob.jobNum, " with a requested job runtime of: ", selectedJob.requestedRunTime, " seconds\n");
                outputfile << "Running job " << selectedJob.jobNum << " with a submit time of: " << selectedJob.submitTime << " seconds on node: " << selectedNodeID << "\n";
                outputfile << "Running job " << selectedJob.jobNum << " with a start time of: " << selectedJob.startTime << " seconds\n";
                outputfile << "Running job " << selectedJob.jobNum << " with a requested job runtime of: " << selectedJob.requestedRunTime << " seconds\n";

                // Calculate the core count actually being used, in addition to the requested number:
                int numCPUsInUse = 0;
                unsigned long memoryInUse = 0;
                for (std::vector<Job>::iterator runningJob = runningJobs.begin(); runningJob != runningJobs.end(); ++runningJob)
                {
                    Job currRunningJob = (*runningJob);
                    numCPUsInUse += currRunningJob.usedCPUs;
                    memoryInUse += currRunningJob.usedMemory;
                }

                // Sum up and obtain the totals for our metrics:
                cbfMetrics.totalCPUsUsed += numCPUsInUse;
                cbfMetrics.maxCPUsUsed = (numCPUsInUse > cbfMetrics.maxCPUsUsed) ? numCPUsInUse : cbfMetrics.maxCPUsUsed;
                cbfMetrics.totalMemoryUsed += memoryInUse;
                cbfMetrics.maxMemoryUsed = (memoryInUse > cbfMetrics.maxMemoryUsed) ? memoryInUse : cbfMetrics.maxMemoryUsed;
            }
            else
            {
                if (currentTime == (*waitingJob).submitTime)
                {
                    // Get node that can hold this job
                    (*waitingJob).nodeID = isJobValid((*waitingJob), nodeList);
                    timestamp newShadowTime = findShadowTimeFromPreceedingJobs(runningJobs, (*waitingJob).nodeID);
                    (*waitingJob).shadowTime = newShadowTime;

                    // if (newShadowTime == NO_JOB_RESERVING)
                    //     (*waitingJob).shadowTime = NO_SHADOW_TIME_ASSIGNED;
                    // else
                    // {
                    //     (*waitingJob).shadowTime = findShadowTimeFromPreceedingJobs(reservingJobs, selectedNodeID);
                    // }
                    reservingJobs.push_back((*waitingJob));
                }
                (*waitingJob).waitTime += 1;
                // If no other job was waiting to start at shadowtime, now we are.
                // if (reservedJobNum == NO_JOB_RESERVING)
                //  {
                //     reservedJobNum = (*waitingJob).jobNum;
                // }
                // Add the rejected job to the waiting list:
                waitingList.push_back(*waitingJob);
            }
        }
        // Recreate the queue with only the processes that are still waiting:
        jobQueue = waitingList;
        // std::cout << "Current time for the CBF algorithm: " << currentTime << std::endl;
        // Increment to the next second: (step-increase in time)
        currentTime++;
        simIteration++;
    }
    // Use finalJobList to calculate metrics.

    outputfile.close();
    return cbfMetrics;
}