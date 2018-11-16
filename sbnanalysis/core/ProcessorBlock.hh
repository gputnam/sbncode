#ifndef __sbnanalysis_core_ProcessorBlock__
#define __sbnanalysis_core_ProcessorBlock__

/**
 * \file ProcessorBlock.hh
 *
 * Processor management.
 *
 * Author: A. Mastbaum <mastbaum@uchicago.edu>, 2018/01/30
 */

#include <string>
#include <vector>
#include "CTPL/ctpl_stl.h"

namespace Json {
  class Value;
}

namespace core {

/**
 * \class core::ProcessorBlock
 * \brief A set of Processors
 */
class ProcessorBlock {
public:
  /** Constructor */
  explicit ProcessorBlock(unsigned n_groups = 0);

  /** Destructor */
  virtual ~ProcessorBlock();

  /**
   * Add a processor to the block.
   *
   * Note that the ProcessorBlock takes ownership of the Processor.
   *
   * \param processor The processor
   * \param config The configuration, if any
   */
  virtual void AddProcessor(export_table* table, Json::Value* config);

  /**
   * Process a set of files.
   *
   * \param filenames A list of art ROOT files to process
   */
  virtual void ProcessFiles(std::vector<std::string> filenames);

  /** Delete all processors owned by the block. */
  virtual void DeleteProcessors();

protected:
  /** For multithreaded case -- try dispatching a processor group */
  bool TryDisptach(ctpl::thread_pool &pool, std::string filename);
  /** Process a list of files on a specified processor group */
  void runProcessorGroup(unsigned group_id, std::vector<std::string> &filenames);
  pid_t runHaddOutput(unsigned proc_id);


  typedef std::vector<std::pair<ProcessorBase*, Json::Value*>> ProcessorGroup;

  // number of processor groups
  unsigned fNGroups;

  /** Processors and their configurations. */
  // each index in the external vector has an instance of each Processor
  std::vector<ProcessorGroup> fProcessorGroups;

  /** list of output files for each processor */
  std::vector<std::vector<std::string>> fOutputFiles;

  // final output name for each processor */
  std::vector<std::string> fOutputFileBase;
  
  // Whether each processor group is ready for the next file
  std::vector<bool> fProcessorGroupReady;

  /** Mutex to guard setting / checking whether processor group is ready */
  std::mutex fProcessorGroupsMutex;
  /** wake up controller thread when ready */
  std::condition_variable fWaitCV;

};

}  // namespace core

#endif  // __sbnanalysis_core_ProcessorBlock__

