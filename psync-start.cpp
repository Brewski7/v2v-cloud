/*
  Handles the process to fetch the file notified via a PSync Update.

  @author Waldo Jordaan
*/

#include <PSync/full-producer.hpp>
#include <ndn-cxx/face.hpp>
#include <ndn-cxx/security/key-chain.hpp>
#include <ndn-cxx/util/logger.hpp>
#include <ndn-cxx/util/scheduler.hpp>
#include <iostream>
#include <ndn-cxx/util/segment-fetcher.hpp>
#include <ndn-cxx/security/validator-null.hpp>

// for execCmd()
#include <cstdio>
#include <memory>
#include <stdexcept>
#include <string>
#include <array>

#include <filesystem>
#include <thread>

std::string GETFILE = "./getfile.py";
std::string GETLATEST = "./get-latest.py";
std::string PUTFILE = "./putfile.py";

NDN_LOG_INIT(PSync.Start);
using namespace ndn::time_literals;

namespace fs = std::filesystem;

std::string execCmd(const std::string& cmd);

fs::path PRIMARY_PATH = "/home/brewski";
fs::path FALLBACK_PATH = "/home/brewski/masters";
fs::path WATCH_DIR;

void initWatchDir()
{
  if (fs::exists(FALLBACK_PATH)) {
    WATCH_DIR = FALLBACK_PATH / "bmw";  // running on laptop
  } else {
    WATCH_DIR = PRIMARY_PATH / "bmw";   // running on RPi
  }

  std::cout << "[Init] WATCH_DIR set to: " << WATCH_DIR << std::endl;
}

class SyncListener
{
public:
  SyncListener(const ndn::Name& syncPrefix, const std::string& userPrefix)
    : m_producer(m_face, m_keyChain, syncPrefix, [this] {
        psync::FullProducer::Options opts;
        opts.onUpdate = std::bind(&SyncListener::processSyncUpdate, this, _1);
        opts.syncInterestLifetime = 1600_ms;
        opts.syncDataFreshness = 1600_ms;
        return opts;
      }())
    , m_userPrefix(userPrefix)
  {
    m_producer.addUserNode(m_userPrefix);

    NDN_LOG_INFO("Sync listener started with prefix: " << m_userPrefix);
  }

  void run()
  {
    m_face.processEvents();
  }

private:
  void processSyncUpdate(const std::vector<psync::MissingDataInfo>& updates)
  {
    for (const auto& update : updates) {
      for (uint64_t i = update.lowSeq; i <= update.highSeq; ++i) {
        NDN_LOG_INFO("Received update: " << update.prefix << "/" << i);
        // Optional: React to update, fetch content, notify, etc.

        ndn::Name name = update.prefix;
        //name.appendSegment(i-1);

        NDN_LOG_INFO("Update received: " << name);
        

        ndn::Name genericPrefix;
        for (const auto& comp : name) {
          if (comp.isGeneric()) {
            genericPrefix.append(comp);
          } else {
            break;
          }
        }

        std::cout << "[INFO]: Name.toUri " << name.toUri() << std::endl;
        std::cout << "[INFO]: Generic Prefix " << genericPrefix.toUri() << std::endl;
        std::string latest = execCmd("python3 get-latest.py -n " + genericPrefix.toUri());
        std::cout << "[INFO]: " << latest << std::endl;
        
        std::string currentName = name.toUri();
        currentName.erase(std::remove_if(currentName.begin(), currentName.end(), ::isspace), currentName.end());

        if (latest == currentName) {
          std::cout << "[Skip] Already have latest version: " << name << std::endl;
          return;
        }
        else{
          std::cout << "[INFO] Strings do not match! " << std::endl;

          std::cout << "[DEBUG] latest.size()=" << latest.size()
          << ", currentName.size()=" << currentName.size() << std::endl;

          for (size_t i = 0; i < std::min(latest.size(), currentName.size()); ++i) {
              std::cout << "[" << i << "] " << int(latest[i]) << " vs " << int(currentName[i]) << std::endl;
          }
        }

          // Step 1: erase from CS asynchronously
        std::thread([name] {
          std::string cmd = "nfdc cs erase " + name.toUri();
          int result = std::system(cmd.c_str());
          if (result != 0) {
            NDN_LOG_WARN("CS erase failed for " << name);
          }
        }).detach();

        // Step 2: schedule fetch and repo insert without blocking main loop
        m_scheduler.schedule(ndn::time::milliseconds(500), [this, name] {
          std::thread([this, name] {
            if (fetchFile(name)) {
              auto [prefix, filepath, timestamp] = splitNameComponents(name);
              putFile(filepath, prefix, timestamp);
            }
          }).detach();
        });
      }
    }
  }

  bool fetchFile(const ndn::Name& name)
  {
    std::string cmd = "python3 " + GETFILE + " -r bmw -n " + name.toUri();
    int ret = std::system(cmd.c_str());

    if (ret == 0) {
      std::cout << "Fetched file via getfile.py: " << name << std::endl;
      return true;
    }
    else {
      NDN_LOG_WARN("getfile.py failed for " << name);
      return false;
    }
  }

  void putFile(const std::string& filepath, const std::string& namePrefix, uint64_t timestamp)
  {
    std::string cmd = "python3 " + PUTFILE +
                      " -r bmw" +
                      " -f " + filepath +
                      " -n " + namePrefix +
                      " --timestamp " + std::to_string(timestamp);

    std::cout << "[PutFile] Running: " << cmd << std::endl;
    int ret = std::system(cmd.c_str());

    if (ret != 0) {
      std::cerr << "[PutFile Error] putfile.py failed for " << filepath << std::endl;
    }
  }

  std::tuple<std::string, std::string, uint64_t> splitNameComponents(const ndn::Name& name)
  {

    ndn::Name genericPrefix;
    uint64_t ts;

    for (const auto& comp : name) {
      if (comp.isGeneric()) {
        genericPrefix.append(comp);
      } 
      else if (comp.isTimestamp()){
        ts = comp.toNumber();
      }
      else {
        break;
      }
    }

    std::string fullPath = WATCH_DIR.string() + genericPrefix.toUri();
    return {genericPrefix.toUri(), fullPath, ts};
  }


private:
  ndn::Face m_face;
  ndn::KeyChain m_keyChain;
  ndn::Scheduler m_scheduler{m_face.getIoContext()};

  psync::FullProducer m_producer;
  ndn::Name m_userPrefix;
  //ndn::security::ValidatorNull m_validator;

};

std::string execCmd(const std::string& cmd) {
    std::array<char, 128> buffer;
    std::string result;

    // Open the command for reading
    std::unique_ptr<FILE, decltype(&pclose)> pipe(popen(cmd.c_str(), "r"), pclose);
    if (!pipe) {
        std::cerr << "[Error] Failed to run command: " << cmd << std::endl;
        return "";
    }

    // Read output into result
    while (fgets(buffer.data(), buffer.size(), pipe.get()) != nullptr) {
        result += buffer.data();
    }

    result.erase(std::remove_if(result.begin(), result.end(), ::isspace), result.end());

    return result;
}

int main(int argc, char* argv[])
{
  if (argc != 3) {
    std::cerr << "Usage: " << argv[0] << " <sync-prefix> <user-prefix>\n";
    return 1;
  }

  initWatchDir();  // Detect platform and set WATCH_DIR

  try {
    SyncListener listener(argv[1], argv[2]);
    listener.run();
  }
  catch (const std::exception& e) {
    NDN_LOG_ERROR(e.what());
    return 1;
  }
}
