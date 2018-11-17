#include <fstream>
#include <iostream>
#include <cstdio>
#include <cstdlib>
#include <vector>

#include <thread>
#include <TROOT.h>
#include <TThread.h>
#include "gallery/Event.h"



std::vector<std::string> filenames;

void *galleryTThread(void *inp) {
  (void) inp;
  for (gallery::Event ev(filenames); !ev.atEnd(); ev.next()) {}
  return NULL;
}

void galleryThread() {
  ROOT::EnableThreadSafety();
  for (gallery::Event ev(filenames); !ev.atEnd(); ev.next()) {}
}


int main(int argc, char* argv[]) {
  // Process input file definition
  std::string filedef = argv[1];
  std::string list_suffix = ".list";

  if (std::equal(list_suffix.rbegin(), list_suffix.rend(), filedef.rbegin())) {
    // File list
    std::ifstream infile(filedef);
    std::string filename;
    while (infile >> filename) {
      filenames.push_back(filename);
    }
  }
  else {
    // Files listed on command line
    for (int i=optind; i<argc; i++) {
      filenames.push_back(argv[i]);
    }
  }

  assert(!filenames.empty());

  // A: works
  // ROOT::EnableThreadSafety();
  // galleryThread(std::move(filenames));
  
  // B: works 
  // std::thread t(galleryThread);
  // t.join();
  //
  // C: doesn't work
  ROOT::EnableThreadSafety();
  std::thread t(galleryThread);
  t.join();

  // C: doesn't work
  // TThread t0(galleryTThread);
  // t0.Run();
  // t0.Join();

  return 0;
}

