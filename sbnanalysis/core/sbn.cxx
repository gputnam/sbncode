#include <fstream>
#include <iostream>
#include <cstdio>
#include <cstdlib>
#include <map>
#include <vector>
#include <dlfcn.h>
#include <json/json.h>
#include <core/ProcessorBase.hh>
#include <core/ProcessorBlock.hh>
#include <core/Loader.hh>

int main(int argc, char* argv[]) {
  // Parse command line arguments
  std::vector<char*> processors;
  std::map<unsigned, char*> config_names;
  int split_index = -1;
  int split_number = -1;

  int c;
  unsigned procindex = 0;
  while ((c=getopt(argc, argv, "m:c:s:i:")) != -1) {
    switch (c) {
      case 'm':
        processors.push_back(optarg);
        procindex++;
        break;
      case 'c':
        config_names[procindex-1] = optarg;
        break;
      case 's':
        split_number = std::stoi(std::string(optarg));
        break;
      case 'i':
        split_index = std::stoi(std::string(optarg));
        break;
      case '?':
        if (optopt == 'c' || optopt == 'm' || optopt == 's' || optopt == 'i')
          fprintf(stderr, "Option -%c requires an argument.\n", optopt);
        else if (isprint(optopt))
          fprintf(stderr, "Unknown option `-%c'.\n", optopt);
        else
          fprintf(stderr, "Unknown option character `\\x%x'.\n", optopt);
        return 1;
      default:
        abort();
    }
  }

  if (argc - optind < 1) {
    std::cout << "Usage: " << argv[0] << " [-m PROCESSOR [-c CONFIG]] -s SPLIT_NUMBER -i SPLIT_INDEX"
              << "INPUTDEF [...]" << std::endl;
    return 0;
  }

  // Process input file definition
  std::string filedef = argv[optind];
  std::string list_suffix = ".list";
  std::vector<std::string> filenames;

  int file_ind = 0;
  if (std::equal(list_suffix.rbegin(), list_suffix.rend(), filedef.rbegin())) {
    // File list
    std::ifstream infile(filedef);
    std::string filename;
    while (infile >> filename) {
      // Add every file if no split is specified.
      // If a split is specified, only add the files configured to this index 
      if ((split_index == -1 || split_number == -1) || (file_ind % split_number) == split_index) {
        filenames.push_back(filename);
      }
      file_ind ++;
    }
  }
  else {
    // Files listed on command line
    for (int i=optind; i<argc; i++) {
      // Add every file if no split is specified.
      // If a split is specified, only add the files configured to this index 
      if ((split_index == -1 || split_number == -1) || (file_ind % split_number) == split_index) {
        filenames.push_back(argv[i]);
      }
      file_ind ++;
    }
  }

  assert(!filenames.empty());

  // Setup
  std::vector<core::ProcessorBase*> procs(processors.size());
  std::vector<Json::Value*> configs(processors.size());

  std::cout << "Configuring... " << std::endl;
  for (size_t i=0; i<procs.size(); i++) {
    core::export_table* exp = core::LoadProcessor(processors[i]);
    procs[i] = exp->create();
    configs[i] = core::LoadConfig(config_names[i]);
  }

  core::ProcessorBlock block;
  for (int i=0; i<procs.size(); i++) {
    block.AddProcessor(procs[i], configs[i]);
  }

  std::cout << "Running... " << std::endl;
  block.ProcessFiles(filenames);

  block.DeleteProcessors();
  std::cout << "Done!" << std::endl;

  return 0;
}

