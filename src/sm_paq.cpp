
#include <cassert>
#include <iostream>

#include <boost/program_options.hpp>
#include <ENCRYPTO_utils/crypto/crypto.h>
#include <ENCRYPTO_utils/parse_options.h>
#include "abycore/aby/abyparty.h"

#include "common/functionalities.h"
#include "ENCRYPTO_utils/connection.h"
#include "ENCRYPTO_utils/socket.h"
#include "common/config.h"
#include <fstream>
#include <vector>

#include "common/VRF.hpp"
#include "common/Timer.hpp"
#include "common/utils.hpp"

globalData<ENCRYPTO::PsiAnalyticsContext> clientContexts;
globalData<ENCRYPTO::PsiAnalyticsContext> serverContexts;
std::mutex m;


globalFlag flagOfWait;

auto read_test_options(int32_t argcp, char **argvp) {
  namespace po = boost::program_options;
  ENCRYPTO::PsiAnalyticsContext context;
  po::options_description allowed("Allowed options");
  std::string type;
  // clang-format off
  allowed.add_options()("help,h", "produce this message")
  ("role,r",         po::value<decltype(context.role)>(&context.role)->required(),                                  "Role of the node")
  ("n,n",        po::value<decltype(context.n)>(&context.n)->default_value(8u),                   "Number of server")
  ("g,g",        po::value<decltype(context.g)>(&context.g)->default_value(5u),                   "g")
  ("cneles,c",        po::value<decltype(context.cneles)>(&context.cneles)->default_value(1u),                   "Number of server elements")
  ("sneles,s",        po::value<decltype(context.sneles)>(&context.sneles)->default_value(1024u),                  "Number of server elements")
  ("bit-length,b",   po::value<decltype(context.bitlen)>(&context.bitlen)->default_value(58u),                  "Bit-length of the elements")
  ("epsilon,e",      po::value<decltype(context.epsilon)>(&context.epsilon)->default_value(1.0f),                  "Epsilon, a table size multiplier")
  ("hint-epsilon,E",      po::value<decltype(context.fepsilon)>(&context.fepsilon)->default_value(1.27f),       "Epsilon, a hint table size multiplier")
  ("address,a",      po::value<decltype(context.address)>(&context.address)->default_value("0.0.0.0"),            "IP address of the server")
  ("port,p",         po::value<decltype(context.port)>(&context.port)->default_value(7777),                         "Port of the server")
  ("radix,m",    po::value<decltype(context.radix)>(&context.radix)->default_value(5u),                             "Radix in PSM Protocol")
  ("functions,f",    po::value<decltype(context.nfuns)>(&context.nfuns)->default_value(3u),                         "Number of hash functions in hash tables")
  ("hint-functions,F",    po::value<decltype(context.ffuns)>(&context.ffuns)->default_value(3u),                         "Number of hash functions in hint hash tables")
  ("psm-type,y",         po::value<std::string>(&type)->default_value("SMPAQ1"),                                   "PSM type {SMPAQ1, SMPAQ2}");
  // clang-format on

  po::variables_map vm;
  try {
    po::store(po::parse_command_line(argcp, argvp, allowed), vm);
    po::notify(vm);
  } catch (const boost::exception_detail::clone_impl<boost::exception_detail::error_info_injector<
               boost::program_options::required_option> > &e) {
    if (!vm.count("help")) {
      std::cout << e.what() << std::endl;
      std::cout << allowed << std::endl;
      exit(EXIT_FAILURE);
    }
  }

  if (vm.count("help")) {
    std::cout << allowed << "\n";
    exit(EXIT_SUCCESS);
  }

  if (type.compare("SMPAQ1") == 0) {
    context.psm_type = ENCRYPTO::PsiAnalyticsContext::SMPAQ1;
  } else if (type.compare("SMPAQ2") == 0) {
    context.psm_type = ENCRYPTO::PsiAnalyticsContext::SMPAQ2;
  } 
   else {
    std::string error_msg(std::string("Unknown SMPAQ type: " + type));
    throw std::runtime_error(error_msg.c_str());
  }

  context.cnbins = 1u;
  context.snbins = context.n;
  context.nbins = context.sneles * context.epsilon;

  return context;
}


void thread(ENCRYPTO::PsiAnalyticsContext context,std::vector<uint64_t> inputs)
{

  std::unique_ptr<CSocket> sock=ENCRYPTO::EstablishConnection(context.address, context.port, static_cast<e_role>(context.role));

  sci::NetIO* ioArr[2];
  osuCrypto::IOService ios;
  osuCrypto::Channel chl;
  osuCrypto::Session* ep;
  std::string name = "n"+context.port;

  if(context.role == SERVER)
  {
    ep= new osuCrypto::Session(ios, context.address, context.port + 3*context.n, osuCrypto::SessionMode::Server,
                          name);
    chl = ep->addChannel(name, name);
  }
  else
  {

    ep = new osuCrypto::Session(ios, context.address, context.port +3*context.n, osuCrypto::SessionMode::Client,
                          name);
    chl = ep->addChannel(name, name);
  }
    ResetCommunication(sock, chl, context);

    flagOfWait++;
    waitFor(flagOfWait,[=](){},context.index==0,context.n);

    run_smpaq(inputs, context, sock, chl);
    AccumulateCommunicationPSI(sock,chl,context);

  m.lock();
  PrintCommunication(context);
  m.unlock();

  if(context.role==SERVER)
    serverContexts.add(context,context.index);
  else
    clientContexts.add(context,context.index);

  sock->Close();

  chl.close();
  ep->stop();
  ios.stop();
  
  delete ep;
}

ENCRYPTO::PsiAnalyticsContext average(const std::vector<ENCRYPTO::PsiAnalyticsContext>& contexts)
{
  if(contexts.empty())
    return ENCRYPTO::PsiAnalyticsContext();
  
  ENCRYPTO::PsiAnalyticsContext context;
  context.role=contexts[0].role;
  context.n=contexts[0].n;
  context.psm_type=contexts[0].psm_type;

  //std::cout << "**********the context.size() is" << contexts[1].timings.base_ots_libote << std::endl;
  context.timings.base_ots_libote = 0;
  context.timings.base_ots_libote2=0;
  // context.timings.base_ots_libote = 0;

  if (context.psm_type==context.SMPAQ1)
    {
      for(int i=0;i<contexts.size();i++)
  {

    // std::cout<<"base ots libote is "<<context.timings.base_ots_libote<< std::endl;
    // std::cout << "the context.size() is" << contexts.size() << std::endl;
    
      if(i!=contexts.size()-1)
      {
        context.timings.base_ots_libote+=contexts[i].timings.base_ots_libote;
        context.timings.oprf1+=contexts[i].timings.oprf1;
      }

    //std::cout<<"base ots libote is "<<context.timings.base_ots_libote<< std::endl;
     context.timings.base_ots_sci+=contexts[i].timings.base_ots_sci;
    context.timings.hint_computation+=contexts[i].timings.hint_computation;
    if(contexts[i].role==SERVER)
    {
      context.timings.encrypt+=contexts[i].timings.encrypt;
    } 
    else if(contexts[i].role==CLIENT&&contexts[i].index==0)
    {
      context.timings.decrypt=contexts[i].timings.decrypt;
    }

    #if 1
      m.lock();
      // std::cout<<(contexts[i].role==SERVER?"Server":"Client")<<" "<<std::to_string(contexts[i].index)<<" "<<contexts[i].timings.base_ots_libote<<"\n";
      m.unlock();
    #endif

  }
    context.timings.oprf1=context.timings.oprf1/(context.n-1);
    //std::cout<<"the final oprf1 is "<<context.timings.oprf1<<std::endl;
    context.timings.oprf2=contexts[context.n-1].timings.oprf2;
    //std::cout<<"the final oprf2 is "<<context.timings.oprf2<<std::endl;
    context.timings.encrypt=context.timings.encrypt/context.n;
    context.timings.hint_computation=context.timings.hint_computation/context.n;
    context.timings.base_ots_libote=context.timings.base_ots_libote/(context.n-1);
    //std::cout<<"the final base ots libote is "<<context.timings.base_ots_libote<< std::endl;
    context.timings.base_ots_libote2=contexts[context.n-1].timings.base_ots_libote2;
    //std::cout << "contexts[n-1].base ot2 time: " << context.timings.base_ots_libote2<<"\n"<<std::endl;
    context.timings.base_ots_sci=context.timings.base_ots_sci/context.n;
    //std::cout<<"XXXXXbase_ots_sci time is "<<context.timings.base_ots_sci<<std::endl;
    // std::cout<<"base_ots_sci time is "<<context.timings.base_ots_sci<<std::endl;
    // std::cout << "contexts[n-1].base ot time: " << contexts[context.n - 1].timings.base_ots_libote<<"\n"<<std::endl;


    context.timings.psm = contexts[0].timings.psm;
    context.timings.total=contexts[0].timings.total;
    context.timings.addtime=contexts[0].timings.addtime;
    // std::cout<<"server 0 add time is: "<<context.timings.addtime<<std::endl;
    context.timings.totalWithoutOT = context.timings.total - context.timings.base_ots_libote -
                                     context.timings.base_ots_libote2 -
                                     context.timings.base_ots_sci;

    if (context.role == SERVER && context.psm_type == context.SMPAQ1)
      context.timings.encrypt=context.timings.encrypt/context.n;
 
      
    }else if(context.psm_type==context.SMPAQ2){ 
      for(int i=0;i<contexts.size();i++)
  {
    //std::cout<<"i is"<<i<<std::endl;
    
    // std::cout<<"base ots libote is "<<context.timings.base_ots_libote<< std::endl;
    // std::cout << "the context.size() is" << contexts.size() << std::endl;
    

      context.timings.oprf1+=contexts[i].timings.oprf1;
      context.timings.base_ots_libote+=contexts[i].timings.base_ots_libote;
      if(i==0 || i==contexts.size()-1)
      context.timings.base_ots_libote2+=contexts[i].timings.base_ots_libote2; 

    //std::cout<<"XXXXXXXXXbase ots libote2 is "<<context.timings.base_ots_libote2<< std::endl;


    context.timings.hint_computation+=contexts[i].timings.hint_computation;
    if(contexts[i].role==SERVER)
    {
      context.timings.encrypt+=contexts[i].timings.encrypt;
    } 
    else if(contexts[i].role==CLIENT&&contexts[i].index==0)
    {
      context.timings.decrypt=contexts[i].timings.decrypt;
    }

    #if 1
      m.lock();
      // std::cout<<(contexts[i].role==SERVER?"Server":"Client")<<" "<<std::to_string(contexts[i].index)<<" "<<contexts[i].timings.base_ots_libote<<"\n";
      m.unlock();

    #endif
  }

    context.timings.oprf1=context.timings.oprf1/(context.n);
    context.timings.oprf2=(contexts[context.n-1].timings.oprf2+contexts[0].timings.oprf2)/2;
    //std::cout<<"the final oprf2 is "<<context.timings.oprf2<< std::endl;
    context.timings.hint_computation=context.timings.hint_computation/context.n;
    context.timings.base_ots_libote=context.timings.base_ots_libote/context.n;
    //std::cout<<"the final base ots libote is "<<context.timings.base_ots_libote<< std::endl;
    context.timings.base_ots_libote2=context.timings.base_ots_libote2/2;
    //std::cout<<"the final base ots libote2 is "<<context.timings.base_ots_libote2<< std::endl;
    // std::cout<<"base ots libote is "<<context.timings.base_ots_libote<< std::endl;
    context.timings.base_ots_sci=context.timings.base_ots_sci/context.n;
    // std::cout<<"base_ots_sci time is "<<context.timings.base_ots_sci<<std::endl;
    // std::cout << "contexts[n-1].base ot time: " << contexts[context.n - 1].timings.base_ots_libote<<"\n"<<std::endl;


    //std::cout<<"the contexts[0].times.psm time is "<<contexts[0].timings.psm<<std::endl;
    //std::cout<<"the contexts[n-1].times.psm time is "<<contexts[context.n - 1].timings.psm<<std::endl;
    context.timings.psm = (contexts[0].timings.psm+contexts[context.n - 1].timings.psm)/2;
    context.timings.total=(contexts[0].timings.total+contexts[context.n - 1].timings.total)/2;
    context.timings.addtime=(contexts[0].timings.addtime+contexts[context.n - 1].timings.addtime)/2;
    // std::cout<<"server 0 add time is: "<<context.timings.addtime<<std::endl;
    context.timings.totalWithoutOT = context.timings.psm - context.timings.base_ots_libote -
                                     context.timings.base_ots_libote2 -
                                     context.timings.base_ots_sci;
    }
  return context;
}
int main(int argc, char **argv) {
  auto context = read_test_options(argc, argv);
  auto gen_bitlen = static_cast<std::size_t>(std::ceil(std::log2(context.sneles))) + 3;
  std::vector<uint64_t> inputs;
  if(context.role == CLIENT) {

      for (int i = 0; i < context.cneles; i++) {
      inputs.push_back(4000);
    }
  } else {
      for (int i = 0; i < context.sneles; i++) {
        inputs.push_back(2000*i);
      }
  }

  std::thread* threads[context.n];


  std::vector<size_t> client_sequence;
  std::vector<size_t> server_sequence;

  for(int i=0;i<context.n;i++)
  {
    client_sequence.push_back(i);
    if(i!=0&&i!=context.n-1)
      server_sequence.push_back(i);
  }
  if(context.role==SERVER)
  {
    Timer timer;
    VRF vrf;
    std::vector<size_t> seq=vrf.sequence(context.n);
    int leader_server=seq[0];
    int center_server=seq[1];

    server_sequence.insert(server_sequence.begin()+leader_server,0);
    server_sequence.insert(server_sequence.begin()+center_server,context.n-1);
    
    context.timings.vrf=timer.end();

    #if 1

    std::cout<<"server_seq:";

    for(auto i:server_sequence)
    {
      std::cout<<i<<" ";
    }
    std::cout<<"\n";

    #endif
  }
  else
  {
    #if 0

    std::cout<<"client_seq:";

    for(auto i:client_sequence)
    {
      std::cout<<i<<" ";
    }
    std::cout<<"\n";

    #endif
  }

  for(int i=0;i<context.n;i++)
  {
    ENCRYPTO::PsiAnalyticsContext tmp_context=context;
    if(context.role == SERVER)
    {
      tmp_context.index=server_sequence[i];
      tmp_context.port+=server_sequence[i];

      #ifdef DEBUG

      std::cout<<"Server port: "<<i<<" will be run.\n";

      #endif
    }
    else
    {
      tmp_context.index=client_sequence[i];
      tmp_context.port+=client_sequence[i];

      #ifdef DEBUG

      std::cout<<"Client port: "<<i<<" will be run.\n";

      #endif
    }
    threads[i]=new std::thread(thread,tmp_context,inputs);
    // std::this_thread::sleep_for(std::chrono::milliseconds(1000));
  }

  for(int i=0;i<context.n;i++){
    threads[i]->join();
    delete threads[i];
  }

  const auto& contexts=context.role==SERVER?serverContexts.data():clientContexts.data();
  const auto& tmp_context=average(contexts);
  PrintTimings(tmp_context);

  return EXIT_SUCCESS;
}
