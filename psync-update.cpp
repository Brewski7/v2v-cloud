/*
  Do a psync update to the sync binary

  Code adapted from full-sync example  
  <sync-prefix> is the sync binary shared "repo" name. This should not be the same as the actual repo name! 
  <user-prefix> is the file prefix

  @author Waldo Jordaan
*/

#include <PSync/full-producer.hpp>
#include <ndn-cxx/face.hpp>
#include <ndn-cxx/security/key-chain.hpp>
#include <ndn-cxx/util/logger.hpp>
#include <ndn-cxx/util/scheduler.hpp>
#include <iostream>
#include <boost/asio/io_context.hpp>
#include "termcolor.hpp"

NDN_LOG_INIT(PSync.Update);
using namespace ndn::time_literals;

class Producer
{
public:
  Producer(const ndn::Name& syncPrefix, const std::string& userPrefix)
    : m_producer(m_face, m_keyChain, syncPrefix, [this] {
        psync::FullProducer::Options opts;
        //opts.onUpdate = std::bind(&Producer::processSyncUpdate, this, _1);
        opts.syncInterestLifetime = 1600_ms;
        opts.syncDataFreshness = 1600_ms;
        return opts;
      }())
    
    , m_userPrefix(userPrefix)
  {
    
    m_producer.addUserNode(m_userPrefix);

    m_scheduler.schedule(ndn::time::seconds(1), [this] {
      
      doUpdate(m_userPrefix);

    });
    
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
        NDN_LOG_INFO("SyncUpdate: " << update.prefix << "/" << i);
        std::cout << termcolor::on_bright_yellow << termcolor::red << "Sync update received by Update::Face: " << update.prefix <<  termcolor::reset << std::endl;
        //std::cout << "Sync update received by Update::Face: " << update.prefix << std::endl;
      }
    }

  }

  void doUpdate(const ndn::Name& prefix)
  {
    m_producer.publishName(prefix);

    uint64_t seqNo = m_producer.getSeqNo(prefix).value();
    NDN_LOG_INFO("Publish: " << prefix << "/" << seqNo);
    
     // Always print to console
    std::cout << termcolor::on_bright_white << termcolor::blue << "Sync update published: " << prefix << "/" << seqNo << termcolor::reset << std::endl;
    //std::cout << "Sync update published: " << prefix << "/" << seqNo << std::endl;

    //m_face.shutdown(); // Ends processEvents() loop in main()
    m_scheduler.schedule(ndn::time::seconds(1), [this] {
      m_face.getIoContext().stop();
      //m_face.shutdown();
    });


  }

private:
  ndn::Face m_face;
  ndn::KeyChain m_keyChain;
  ndn::Scheduler m_scheduler{m_face.getIoContext()};

  psync::FullProducer m_producer;
  ndn::Name m_userPrefix;

};


int main(int argc, char* argv[])
{
  if (argc != 3) {
    std::cerr << "Usage: " << argv[0] << " <sync-prefix> <user-prefix> \n";
    return 1;
  }

  try {
    Producer producer(argv[1], argv[2]);
    producer.run();  // Wait for sync state or fallback
  }
  catch (const std::exception& e) {
    NDN_LOG_ERROR(e.what());
  }
}
