#include <sys/wait.h>
#include <string>
#include <vector>
#include <algorithm>
#include <json/json.h>
#include "ProcessorBase.hh"
#include "ProcessorBlock.hh"

namespace core {

ProcessorBlock::ProcessorBlock(unsigned n_groups): fNGroups(n_groups), fProcessorGroupReady(n_groups, false) {}


ProcessorBlock::~ProcessorBlock() {}


void ProcessorBlock::AddProcessor(export_table* table,
                                  Json::Value* config) {
  // add the processor to each group
  for (unsigned i = 0; i < fNGroups; i++) {
    // copy the config for each individual processor
    fProcessorGroups[i].push_back({table->create(), new Json::Value(config)});
    int n_processor = fProcessorGroups[i].size() - 1;
    // set the ID
    fProcessorGroups[i][n_processor].first->fInstanceID = i;
    // set a slot for the output file name
    fOutputFileBase.emplace_back();
    // and for the output file
    fOutputFiles.emplace_back();
  }

}

void ProcessorBlock::runProcessorGroup(unsigned group_id, std::vector<std::string> &filenames) {
  for (gallery::Event ev(filenames); !ev.atEnd(); ev.next()) {
    for (auto it : fProcessorGroups[group_id]) {
      it.first->BuildEventTree(ev);
      
      bool accept = it.first->ProcessEvent(ev, it.first->fEvent->truth, *it.first->fReco);
      
      if (accept) {
      it.first->FillTree();
      }
      
      it.first->EventCleanup();
    }
  }
}

void ProcessorBlock::ProcessFiles(std::vector<std::string> filenames) {
  // Setup
  std::cout << "Setting up: " << fProcessorGroups.size() << " processors." << std::endl;
  for (unsigned group_id = 0; group_id < fProcessorGroups.size(); group_id++) {
    for (unsigned proc_ind = 0; proc_ind < fProcessorGroups[group_id].size(); proc_ind++) {

      auto it = fProcessorGroups[group_id][proc_ind];
      it.first->Setup(it.second);

      // if multiple groups, screw with the output file of the processor
      if (fProcessorGroups.size() > 1) { 
        std::string OutputBaseName = it.first->fOutputFilename;
        std::string thisOutputName = OutputBaseName + "__instance__ " + std::to_string(group_id);

        // save the names
        fOutputFiles[proc_ind].push_back(thisOutputName);
        if (group_id == 0) {
          // NOTE: space in the vector was allocated back in AddProcessor()
          fOutputFileBase[proc_ind] = OutputBaseName;
        }

        // tell the processor instance its new output file name
        it.first->fOutputFilename = thisOutputName;

        std::cout << "Set up instance: " << group_id << " with output filename: " << thisOutputName << std::endl;

      }

      it.first->Initialize(it.second);
    }
  }

  // Event loop -- single threaded
  if (fNGroups == 1) {
    // single instance for each processor, so no need to worry about multithreading
    runProcessorGroup(0, filenames);
  }
  // Event loop -- multi threaded at the input file level
  else {
    // initialize the thread pool -- one for each group
    ctpl::thread_pool pool(fNGroups);

    // dispatch each filename to a processor group
    for (unsigned i = 0; i < filenames.size(); i++) {
      // Try dispatching a file
       
      bool dispatched = false;
      
      // Take the waker lock so that no one does any funny business while 
      // we are checking the locks
      std::unique_lock<std::mutex> lock(fProcessorGroupsMutex);
      while(1) {
        bool dispatched = TryDisptach(pool, filenames[i]);
        // dispatched! Onto the next file
        if (dispatched) break;
        std::cout << "Still looking for processor group for file: " << filenames[i] << std::endl;
        // if it didn't work, everyone is busy 
        // wait for someone to finish
        fWaitCV.wait(lock);
     }
     // drop the lock here
    }
   
  }

  // Finalize
  for (auto &group: fProcessorGroups) {
    for (auto it : group) {
      it.first->Finalize();
      it.first->Teardown();
    }
  }
  // if number of groups > 1, hadd all the output files together
  if (fNGroups > 1 && false) {
    std::vector<pid_t> hadd_pids;
    for (unsigned i = 0; i < fNGroups; i++) {
      hadd_pids.push_back(runHaddOutput(i));
    }
    // wait on the hadd-ing
    while (1) {
      pid_t pid = wait(NULL);
      if (pid < 0) return; /* Will happen if no more children or if (e.g.) Ctrl-C*/
      hadd_pids.erase(std::remove(hadd_pids.begin(), hadd_pids.end(), pid), hadd_pids.end());
    }
  }
}

// NOTE: Should only be called while holding the fProcessorGroupsMutex lock
bool ProcessorBlock::TryDisptach(ctpl::thread_pool &pool, std::string filename) {
  bool dispatched = false;
  // try each processor and push it
  for (unsigned group_id = 0; group_id < fProcessorGroups.size(); group_id++) {
    // see if processor is available
    if (fProcessorGroupReady[group_id]) {
      // We found a ready processor group! Set it as busy
      fProcessorGroupReady[group_id] = false;
      std::cout << "Found processor group (" << group_id << ") for filename: " << filename << std::endl;
      
      // Function which runs the processor
      auto process = [this, filename, group_id](int) {

        unsigned this_id = group_id;

	// process all events
	std::vector<std::string> filelist {filename};
	runProcessorGroup(this_id, filelist);

        // Finished work!
        //
        // Mark as ready and call the condition variable
        std::unique_lock<std::mutex> lock(fProcessorGroupsMutex);
        fProcessorGroupReady[group_id] = true;
        // give up lock before calling condition variable
        lock.unlock();

        fWaitCV.notify_one();
      };
  
      // run in threadpool
      pool.push(process);
      // dispatched!
      dispatched = true;
      break;
    }         
  }
  return dispatched;
}

void ProcessorBlock::DeleteProcessors() {
  for (auto &group: fProcessorGroups) {
    for (auto it : group) {
      delete it.first;
    }
  }
}

pid_t ProcessorBlock::runHaddOutput(unsigned proc_id) {
  std::cout << "Starting process to HADD the output files for processor " << proc_id << "." << std::endl;

  // setup arguments for child

  // arguments
  char *exec_args[fOutputFiles[proc_id].size() + 1 /* for output name */ + 1 /* For STOP */];
  
  // setup command with output files (input to hadd)
  for (unsigned i = 0; i < fOutputFiles[proc_id].size(); i ++) {
    exec_args[i] = strdup(fOutputFiles[proc_id][i].c_str());
  }
  
  // and output name for hadd
  exec_args[fOutputFiles[proc_id].size()] = strdup(fOutputFileBase[proc_id].c_str()); 
  // STOP for last arg
  exec_args[fOutputFiles[proc_id].size()+1] = 0;

  std::cout << "Copying to location: " << fOutputFileBase[proc_id] << std::endl;

  // Multiple instances!
  //
  // merge the output files with hadd
  //
  // fork() and exec()
  int pid = fork();

  // child!
  if (pid == 0) {
    
    int ret = execv("hadd", exec_args);

    // if we're here, something bad happenned....
    if (ret == 0) {
      std::cout << "Something very strange happenned" << std::endl;
    }
    else {
      std::cerr << "ERROR: hadd'ing the output files failed. You must do this yourself manually." << std::endl;
    }
    std::exit(1);
  }
  // Parent process
  //
  // Fork failed :(
  else if (pid < 0) {
    std::cerr << "ERROR: hadd'ing the output files failed. You must do this yourself manually." << std::endl;
  }

  return pid;
}

}  // namespace core

