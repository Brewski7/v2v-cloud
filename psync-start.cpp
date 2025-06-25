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
#include <unistd.h>
#include <PSync/detail/state.hpp>
#include <map>
#include "termcolor.hpp"

// for execCmd()
#include <cstdio>
#include <memory>
#include <stdexcept>
#include <string>
#include <array>
#include <fstream>
#include <vector>
#include <cstdlib>

#include <filesystem>
#include <thread>

std::string GETFILE = "./getfile.py";
std::string GETLATEST = "./get-latest.py";
std::string PUTFILE = "./putfile.py";
std::string DELFILE = "./delfile.py";
const std::string SUBSFILE = "./subsfile";

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
    m_state[m_userPrefix] = 0;

    char hostBuf[256];
    if (gethostname(hostBuf, sizeof(hostBuf)) == 0) {
      m_hostname = hostBuf;
    }

    std::ifstream in(SUBSFILE);
    if (!in.is_open()) {
      std::cout << "Unable to open subscriptions file: " << SUBSFILE << std::endl;
    }
    else {
      std::string line;
      while (std::getline(in, line)) {
        line.erase(0, line.find_first_not_of(" \t\r\n"));
        line.erase(line.find_last_not_of(" \t\r\n") + 1);
        if (line.empty())
          continue;
        //ndn::Name pref(m_hostname);
        //pref.append(ndn::Name(line));
        ndn::Name pref(line);
        m_allowedPrefixes.push_back(pref);
      }
      std::cout << "Loaded " << m_allowedPrefixes.size() << " subscription prefixes from " << SUBSFILE << std::endl;
      for (int i = 0; i < m_allowedPrefixes.size(); i++){
        std::cout <<  m_allowedPrefixes[i] << std::endl;
      }
    }

    std::cout << "Sync listener started with prefix: " << m_userPrefix << " on host " << m_hostname << std::endl;
  }

  void run()
  {
    m_face.processEvents();
  }

private:

  static uint64_t extractTimestamp(const std::string& name)
  {
    auto pos = name.rfind("/t=");
    if (pos == std::string::npos) {
      return 0;
    }
    try {
      return std::stoull(name.substr(pos + 3));
    }
    catch (...) {
      return 0;
    }
  }

  static void deleteFromRepo(const std::string& name)
  {
    std::string cmd = "python3 " + DELFILE + " -r bmw -n " + name;
    int ret = std::system(cmd.c_str());
    if (ret != 0) {
      std::cerr << "[Delete Error] delfile.py failed for " << name << std::endl;
    }
  }

  void processSyncUpdate(const std::vector<psync::MissingDataInfo>& updates)
  {
    
    for (const auto& update : updates) {
      m_state[update.prefix] = update.highSeq;
    }

    for (const auto& update : updates) {
      for (uint64_t i = update.lowSeq; i <= update.highSeq; ++i) {
        NDN_LOG_INFO("Received update: " << update.prefix << "/" << i);
        // Optional: React to update, fetch content, notify, etc.

        ndn::Name name = update.prefix;
        //name.appendSegment(i-1);

        std::cout << termcolor::on_white << termcolor::blue << "Update received: " << name << termcolor::reset << std::endl;
        //std::cout << "Update received: " << name << std::endl;
        
        bool hostnameMatch = true;
        if (!m_hostname.empty()) {
          std::string firstComp = name.size() > 0 ? name.at(0).toUri() : "";
          hostnameMatch = (firstComp == m_hostname);
        }

        bool subsMatch = false;
        if (!m_allowedPrefixes.empty()) {
          for (const auto& p : m_allowedPrefixes) {
            if (p.isPrefixOf(name)) {
              subsMatch = true;
              break;
            }
          }
        }

        if (!hostnameMatch && !subsMatch) {
          std::cout << termcolor::yellow << "Ignoring update for " << name << " on host " << m_hostname << termcolor::reset << std::endl;
          std::cout << "PSync update received but ignored due to hostname and subscription mismatch: " << name << std::endl;
          continue;
        }

        ndn::Name genericPrefix;
        for (const auto& comp : name) {
          if (comp.isGeneric()) {
            genericPrefix.append(comp);
          } else {
            break;
          }
        }

        std::string latest = execCmd("python3 get-latest.py -n " + genericPrefix.toUri());
        
        std::string currentName = name.toUri();
        currentName.erase(std::remove_if(currentName.begin(), currentName.end(), ::isspace), currentName.end());

        uint64_t curTs = extractTimestamp(currentName);
        uint64_t latestTs = extractTimestamp(latest);

        if (!latest.empty() && latestTs >= curTs) {
          std::cout << termcolor::yellow << "[Skip] Already have latest version: " << latest << termcolor::reset << std::endl;
          //std::cout << "[Skip] Already have latest version: " << latest << std::endl;  
          continue;
        }

        if (!latest.empty()) {
          std::thread([latest] {
            deleteFromRepo(latest);
          }).detach();
        }

        // Step 1: erase from CS asynchronously using generic prefix
        std::thread([pref = genericPrefix.toUri()] {
          std::string cmd = "nfdc cs erase " + pref;
          int result = std::system(cmd.c_str());
          if (result != 0) {
            NDN_LOG_WARN("CS erase failed for " << pref);
          }
        }).detach();
        
        // Step 2: schedule fetch and repo insert without blocking main loop
        m_scheduler.schedule(ndn::time::milliseconds(500), [this, name] {
          std::thread([this, name] {
            if (fetchFile(name)) {
              auto [prefix, filepath, timestamp] = splitNameComponents(name);
              putFile(filepath, prefix, timestamp);

              std::cout << termcolor::on_blue << termcolor::white << "Outside cmd statement" << termcolor::reset << std::endl;

              if (prefix.rfind("/cmd", 0) == 0) {
                std::cout << termcolor::on_blue << termcolor::white << "Inside cmd statement" << termcolor::reset << std::endl;
                executeCommand(filepath);
              }

            }
          }).detach();
        });
      }
    }
    psync::detail::State curState;
    for (const auto& [prefix, seq] : m_state) {
      if (seq != 0) {
        curState.addContent(ndn::Name(prefix).appendNumber(seq));
      }
    }
    std::cout << termcolor::on_white << termcolor::blue <<"[SyncState] " << curState << termcolor::reset << std::endl;
    //std::cout << "[SyncState] " << curState << std::endl;
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

  void executeCommand(const std::string& filepath)
  {
    std::string chmodCmd = "chmod +x " + filepath;
    std::system(chmodCmd.c_str());

    std::string runCmd = "bash " + filepath;
    int ret = std::system(runCmd.c_str());
    if (ret != 0) {
      std::cerr << "[Cmd Error] failed to run " << filepath << std::endl;
    }
  }

private:
  ndn::Face m_face;
  ndn::KeyChain m_keyChain;
  ndn::Scheduler m_scheduler{m_face.getIoContext()};

  psync::FullProducer m_producer;
  ndn::Name m_userPrefix;
  //ndn::security::ValidatorNull m_validator;
  std::string m_hostname;
  std::vector<ndn::Name> m_allowedPrefixes;
  std::map<ndn::Name, uint64_t> m_state;
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
