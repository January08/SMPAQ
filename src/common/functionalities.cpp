
#include "functionalities.h"

#include "ENCRYPTO_utils/connection.h"
#include "ENCRYPTO_utils/socket.h"
#include "abycore/sharing/boolsharing.h"
#include "abycore/sharing/sharing.h"
//#include "polynomials/Poly.h"

#include "HashingTables/cuckoo_hashing/cuckoo_hashing.h"
#include "HashingTables/common/hash_table_entry.h"
#include "HashingTables/common/hashing.h"
#include "HashingTables/simple_hashing/simple_hashing.h"
#include "config.h"
#include <algorithm>
#include <chrono>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <memory>
#include <random>
#include <ratio>
#include <unordered_set>
#include <unordered_map>
#include <cmath>
#include <openssl/sha.h>
#include <string>
#include <cstdint>
#include <numeric>
#include <NTL/ZZ.h>

#include "utils.hpp"
#include "Timer.hpp"
#include "Paillier.hpp"
#include "KA.hpp"
#include "fileReader.hpp"
#include "block.hpp"

namespace ENCRYPTO {  
template<class T>
std::vector<T> flatten(const std::vector<std::vector<T>>& data) {
    std::vector<T> flat;
    for (const auto& bucket : data) {
        flat.insert(flat.end(), bucket.begin(), bucket.end());
    }
    return flat;
}

uint64_t generate_random_number(uint64_t max_value) {
    static std::mt19937_64 rng(std::random_device{}()); 
    std::uniform_int_distribution<uint64_t> dist(0, max_value);
    return dist(rng);
}
std::vector<uint64_t> generateData(std::size_t neles, std::size_t index) {
    std::mt19937_64 engine(static_cast<uint64_t>(index));
    std::uniform_int_distribution<uint64_t> dist(0, 1000);

    std::vector<uint64_t> data;
    data.reserve(neles);
    for (std::size_t i = 0; i < neles; ++i) {
        data.push_back(dist(engine));
    }
    return data;
}
std::vector<uint64_t> fromClientOprfData(const std::vector<osuCrypto::block>& data,uint64_t size)
{
  std::vector<std::vector<uint64_t>> client_simple_table1(size);
  for (size_t i = 0; i < data.size() && i < size; ++i) {
      std::vector<uint64_t> xor_result = blockToUint64Xor(data[i]);  
      client_simple_table1[i].insert(client_simple_table1[i].end(), xor_result.begin(), xor_result.end());
      
  }
  return flatten(client_simple_table1);
}

std::vector<std::vector<uint64_t>> fromServerOprfData(const std::vector<std::vector<osuCrypto::block>>& data,uint64_t sneles=8,uint64_t snbins=1)
{
  std::vector<std::vector<uint64_t>> simulated_simple_table_2(snbins);
  size_t input_index1 = 0;

  for (size_t i = 0; i < snbins; ++i) {
      if (i < data.size()) { 
        size_t index1 = 0;
        for (const auto &blk : data[i]) {
          auto xor_result = blockToUint64Xor(blk);  
          if (index1 % sneles == 0 && index1 != 0) {
                  std::vector<uint64_t> newRow; 
                  simulated_simple_table_2.push_back(newRow);
              }
        
            simulated_simple_table_2[i].insert(simulated_simple_table_2[i].end(), xor_result.begin(),
                                              xor_result.end());
        }
      }
  }
  return simulated_simple_table_2;
}

size_t getSharedCount(const std::vector<bool>& isShared)
{
  size_t count=0;

  for(const auto& i : isShared)
    if(!i)
      count++;

  return count;
}


int generateRandomNumber(int min, int max) {
    std::random_device rd;
    std::mt19937 gen(rd());
    std::uniform_int_distribution<> distrib(min, max);
    return distrib(gen);
}


std::tuple<int, int> splitIntoShares(int number) {
    int share1 = generateRandomNumber(1, 100);
    int share2 = number - share1;
    return std::make_tuple(share1, share2);
}


globalData<std::vector<uint64_t>> serverID;
globalData<std::vector<uint64_t>> clientID;
globalData<std::vector<uint64_t>> clientID_center;
globalData<std::vector<uint64_t>> clientID_leader;
globalData<std::vector<uint64_t>> serverID_center;
globalData<std::vector<uint64_t>> serverID_leader;
globalData<std::vector<uint64_t>> serverData_center;
globalData<std::vector<uint64_t>> serverData_leader; 

std::atomic<uint64_t> g_sum_center {0};
std::atomic<uint64_t> g_sum_leader {0};

std::atomic<int> g_received_count {0};

std::atomic<uint64_t> g_total_sum {0};

globalData<std::vector<NTL::ZZ>> serverData;


globalFlag Cf1,Cf2;


globalFlag Sf1,Sf2;


NTL::ZZ ng[2];


globalFlag Sf2_Ng;


Paillier::Paillier* paillier;


// globalData<std::vector<uint64_t>> clientBins;
// globalData<std::vector<uint64_t>> serverBins;


globalData<EVP_PKEY*> serverKeys;

globalFlag Sf_Keys;


globalData<int> clientResult;

globalFlag Cf_Result;


globalData<int> serverResult;

globalFlag Sf_Result;


globalFlag Sf_Share;

std::vector<std::vector<bool>> isShared;


size_t firstShared;
size_t secondShared;

int keyOfShared;



void run_smpaq(const std::vector<std::uint64_t> &inputs, PsiAnalyticsContext &context, std::unique_ptr<CSocket> &sock,osuCrypto::Channel &chl)
{
  Timer totalTime;
  Timer psmTime;

  bool isLeader=context.index==0?true:false;
  bool isCenter=context.index==context.n-1?true:false;

  if (context.role == CLIENT)
  {
    Timer computationTime;

    std::vector<std::vector<uint64_t>> simulated_cuckoo_table(context.cnbins);
    for (auto i = 0ull; i < inputs.size(); ++i) {
        simulated_cuckoo_table[i % context.cnbins].push_back(inputs[i]);
    }

    context.timings.hint_computation=computationTime.end();

psmTime.start();

if(context.psm_type == PsiAnalyticsContext::SMPAQ2){
  std::vector<uint64_t> simulated_simple_table_1 = flatten(simulated_cuckoo_table);

auto oprf_value = ot_receiver(simulated_simple_table_1, chl, context);

auto data = fromClientOprfData(oprf_value, context.cnbins);
clientID.add(data, context.index); 

#ifdef DEBUG

std::vector<std::vector<uint64_t>> tmp;
tmp.push_back(fromClientOprfData(oprf_value, 1));
writeToCSV(tmp, "Client_Oprf1_" + std::to_string(context.index) + ".csv");
#endif
Cf1++; 
waitFor(Cf1, [&]() {
          
            Cf1.reset(0); 
        }, isCenter, context.n); 

Timer secondoprftime;
secondoprftime.start();

if(isCenter||isLeader)
    {
      if(isCenter){
      auto oprf_value = ot_receiver(flatten(clientID.data()), chl, context,context.n);
      
      std::vector<std::vector<uint64_t>> data_center;

      for(const auto& i : oprf_value)
        data_center.push_back(blockToUint64Xor(i));

      for(int i=0;i<data_center.size();i++)
        clientID_center.add(data_center[i],i);

      #ifdef DEBUG

      // write to file

      writeToCSV(clientID_center.data(),"Client_center_Oprf2_"+to_string(context.index)+".csv");

      #endif
      }
      else if(isLeader)
    {
      auto oprf_value = ot_receiver(flatten(clientID.data()), chl, context,context.n);
      
      std::vector<std::vector<uint64_t>> data_leader;

      for(const auto& i : oprf_value)
        data_leader.push_back(blockToUint64Xor(i));

      for(int i=0;i<data_leader.size();i++)
        clientID_leader.add(data_leader[i],i);

      #ifdef DEBUG
      writeToCSV(clientID_leader.data(),"Client_leader_Oprf2_"+to_string(context.index)+".csv");

      #endif
    }
      Cf2++;
    }

  waitFor(Cf2, [&]() {
            Cf2.reset(0); 
        }, isCenter, 2); 

  context.timings.secondoprftime=secondoprftime.end();
  //std::cout<<"secondoprftime is "<<context.timings.secondoprftime<<std::endl;

if(isCenter||isLeader){
   if(isCenter)
    {
      auto data_center=flatten(clientID_center.data());
      sock->Send(data_center.data(),sizeof(uint64_t)*context.n*context.cnbins);
      uint64_t sum_center = 0;
      if(context.psm_type == PsiAnalyticsContext::SMPAQ2)
      {
        
        sock->Receive(&sum_center,sizeof(sum_center));

        //std::cout<<"Center Client Recv sum_center : "<<sum_center<<"\n";

        g_sum_center.store(sum_center);
        g_received_count.fetch_add(1);
      }
     
    }
    else if(isLeader)
    {
      uint64_t sum_leader = 0;
      auto data_leader=flatten(clientID_leader.data());
      sock->Send(data_leader.data(),sizeof(uint64_t)*context.n*context.cnbins);
      if(context.psm_type == PsiAnalyticsContext::SMPAQ2)
      {
        
        sock->Receive(&sum_leader,sizeof(sum_leader));
        g_sum_leader.store(sum_leader);
        g_received_count.fetch_add(1);
      
      }
    }
    Cf2++;
}
waitFor(Cf2, [&]() {
                if (g_received_count.load() == 2) {
                    uint64_t final_sum_center = g_sum_center.load();
                    uint64_t final_sum_leader = g_sum_leader.load();
                    uint64_t total = final_sum_center + final_sum_leader;
                    g_total_sum.store(total); 

                    std::cout << "Center Sum = " << final_sum_center
                              << ", Leader Sum = " << final_sum_leader
                              << ", Total Sum = " << total << std::endl;
                    g_sum_center.store(0);
                    g_sum_leader.store(0);
                    g_received_count.store(0); 

                } else {
                    std::cerr << "warn: waitFor but g_received_count ("
                              << g_received_count.load() << ") != 2. may be error" << std::endl;
                }

                Cf2.reset(0); 

            }, isCenter, 2); 

}
else if(context.psm_type == PsiAnalyticsContext::SMPAQ1){
    if(isLeader)
      {
        paillier=new Paillier::Paillier(1024);
        ng[0]=paillier->getN();
        ng[1]=paillier->getG();
        std::stringstream ss;
        ss << ng[0]<<"|"<<ng[1];

        std::string str=ss.str();
        uint64_t len=str.size();
        sock->Send(&len,sizeof(uint64_t));
        sock->Send(str.data(),sizeof(char)*len);
      }
   std::vector<uint64_t> simulated_simple_table_1 = flatten(simulated_cuckoo_table);
   psmTime.start();
   if(!isCenter)
    {
      auto oprf_value = ot_receiver(simulated_simple_table_1, chl, context);

      auto data=fromClientOprfData(oprf_value,context.cnbins);

      clientID.add(data,context.index);

      #ifdef DEBUG

      // write to file

      std::vector<std::vector<uint64_t>> tmp;
      tmp.push_back(fromClientOprfData(oprf_value,1));

      writeToCSV(tmp,"Client_Oprf1_"+to_string(context.index)+".csv");

      #endif

      #if 1

      std::cout<<"Client "<<to_string(context.index)<<" flag:"<<to_string(Cf1++)<<"\n";

      #else

      Cf1++;
      std::cout << "Cf1 value after increment: " << Cf1.get() << std::endl;


      #endif
    }
    waitFor(Cf1,[&]()
    {
      clientID.add(simulated_simple_table_1,context.index);
    },isCenter,context.n-1);
    if(isCenter)
    {
      auto oprf_value = ot_receiver(flatten(clientID.data()), chl, context,context.n);
      
      std::vector<std::vector<uint64_t>> data;

      for(const auto& i : oprf_value)
        data.push_back(blockToUint64Xor(i));

      for(int i=0;i<data.size();i++)
        clientID.add(data[i],i);

      #ifdef DEBUG

      // write to file

      writeToCSV(clientID.data(),"Client_Oprf2_"+to_string(context.index)+".csv");

      #endif
    }
    else
    {
      #if 0

      std::cout<<"Client "<<to_string(context.index)<<" flag:"<<to_string(Cf2++)<<"\n";

      #else

      Cf2++;

      #endif
    }
     waitFor(Cf2,[=]()
    {
      ;
    },isCenter,context.n-1);
    if(isLeader)
    {
      auto data=flatten(clientID.data());
      sock->Send(data.data(),sizeof(uint64_t)*context.n*context.cnbins);
      if(context.psm_type == PsiAnalyticsContext::SMPAQ2)
      {
        uint64_t num;
        sock->Receive(&num,sizeof(num));

        std::cout<<"Leader Client Recv num : "<<num<<"\n";
      
      }
      else if(context.psm_type == PsiAnalyticsContext::SMPAQ1)
      {
        uint64_t len;
        sock->Receive(&len,sizeof(uint64_t));

        char* str=new char[len];
        sock->Receive(str,sizeof(char)*len);

        std::string sum_str(str);

        delete[] str;

        NTL::ZZ sum=NTL::conv<NTL::ZZ>(sum_str.c_str());

        #if 0

        std::cout<<"Leader Client Recv sum : "<<sum<<"\n";

        #endif
        Timer decryptTime;

        NTL::ZZ original_sum=Paillier::decryptNumber(sum,paillier->getN(),paillier->getLambda(),paillier->getLambdaInverse());
        
        context.timings.decrypt=decryptTime.end();

        #if 1

        std::cout<<"original_sum : "<<original_sum<<"\n";

        #endif

        delete paillier;
      }
    }
}    
  }
  else
  {
    Timer computationTime;

    std::vector<std::vector<uint64_t>> simulated_simple_table_1(context.snbins);
    size_t input_index = 0;
    for (size_t i = 0; i < context.snbins && input_index < inputs.size(); ++i) {
        for (size_t j = 0; j < context.sneles;++j) {
          simulated_simple_table_1[i].push_back(inputs[input_index++]);
        }
    }

    context.timings.hint_computation=computationTime.end();

    Timer encryptTime;
    Timer wholeoprf;

psmTime.start();
if(context.psm_type == PsiAnalyticsContext::SMPAQ2)
{

auto oprf_value = ot_sender(simulated_simple_table_1, chl, context);

auto raw_data = fromServerOprfData(oprf_value, context.sneles, 1);

#ifdef DEBUG

writeToCSV(raw_data, "Server_Oprf1_" + std::to_string(context.index) + ".csv");
#endif

auto data = raw_data[0]; 
serverID.add(data, context.index); 
//writeToCSV(serverID.data(), "ServerIDdata" + std::to_string(context.index) + ".csv");

Sf1++; 
//std::cout << "Sf1 value after increment: " << Sf1.get() << std::endl; 
  waitFor(Sf1, [&]() {
            Sf1.reset(0); 
       }, isCenter, context.n); 
  std::vector<uint64_t> originalData = generateData(context.sneles, context.index);
  std::vector<uint64_t> shareC, shareL;
  std::random_device rd; 
  std::mt19937_64 gen(rd());
  std::uniform_int_distribution<uint64_t> dis(0, UINT64_MAX);
  for (auto v : originalData) {
    uint64_t s1 = dis(gen);
    uint64_t s2 = v - s1;
    shareC.push_back(s1);
    shareL.push_back(s2);
  }
 serverData_center.add(shareC, context.index);
 serverData_leader.add(shareL, context.index);

  Sf2++;
  waitFor(Sf2, [](){}, isLeader, context.n);

 if(isCenter|| isLeader)
    {
      if(isCenter){
        auto oprf_value_center = ot_sender(serverID.data(), chl, context, context.n);

      //
      auto data_center=fromServerOprfData(oprf_value_center,context.sneles,context.sneles);

      #ifdef DEBUG

      writeToCSV(data_center,"Server__center_Oprf2_"+to_string(context.index)+".csv");

      #endif
      for(int i=0;i<data_center.size();i++)
        serverID_center.add(data_center[i],i);
      }
      else if (isLeader) {
      auto oprf_value_leader = ot_sender(serverID.data(), chl, context, context.n);

      //
      auto data_leader=fromServerOprfData(oprf_value_leader,context.sneles,context.sneles);
      writeToCSV(data_leader,"Server__leader_Oprf2_"+to_string(context.index)+".csv");

      #ifdef DEBUG

      writeToCSV(data_leader,"Server__leader_Oprf2_"+to_string(context.index)+".csv");

      #endif
      for(int i=0;i<data_leader.size();i++)
        serverID_leader.add(data_leader[i],i);
    }
    Sf2++;

    }
     waitFor(Sf2, [&]() {
            Sf2.reset(0); 
        }, isCenter, 2); 

    context.timings.wholeoprf=wholeoprf.end();
    // std::cout<<"wholeoprf time is "<<wholeoprf.end()<<"\n";
    if(isCenter)
    {
      std::vector<uint64_t> dataOfClient_center(context.n*context.cnbins);
      std::vector<std::vector<uint64_t>> dataOfServer_center=serverID_center.data();
      auto allShare_center = serverData_center.data();

      sock->Receive(dataOfClient_center.data(),sizeof(uint64_t)*context.cnbins*context.n);
      writeToCSV(dataOfServer_center,"Server_Center_ID.csv");

      #ifdef DEBUG

      writeToCSV(dataOfServer_center,"Server_Center_ID.csv");
      writeToCSV(serverData.data(),"Server_All_EncryptData.csv");
      std::vector<std::vector<uint64_t>> tmp;
      tmp.push_back(dataOfClient);
      writeToCSV(tmp,"Client_All_ID.csv");

      #endif
      if(context.psm_type == PsiAnalyticsContext::SMPAQ2)
    
      {
        Timer search_center;
        uint64_t sum_center=0;

        for(int i=0;i<dataOfClient_center.size();i++)
        {
          for(int j=0;j<dataOfServer_center.size();j++)
          {
            for(int k=0;k<dataOfServer_center[j].size();k++)
            {
              if(dataOfClient_center[i]==dataOfServer_center[j][k])
                sum_center += allShare_center[j][k]; 
            }
          }
        }
        std::cout<<"sum_center is"<<sum_center<<std::endl;

        sock->Send(&sum_center,sizeof(sum_center));
        context.timings.search=search_center.end();
      }
    }

    else if(isLeader)
    {
      std::vector<uint64_t> dataOfClient_leader(context.n*context.cnbins);
      std::vector<std::vector<uint64_t>> dataOfServer_leader=serverID_leader.data();
      auto allShare_leader = serverData_leader.data();

      sock->Receive(dataOfClient_leader.data(),sizeof(uint64_t)*context.cnbins*context.n);
      writeToCSV(dataOfServer_leader,"Server_Leader_ID.csv");

      #ifdef DEBUG

      writeToCSV(dataOfServer_leader,"Server_Leader_ID.csv");
      writeToCSV(serverData.data(),"Server_All_EncryptData.csv");
      std::vector<std::vector<uint64_t>> tmp;
      tmp.push_back(dataOfClient);
      writeToCSV(tmp,"Client_All_ID.csv");

      #endif
      if(context.psm_type == PsiAnalyticsContext::SMPAQ2)
    
      {
        Timer search_leader;
        uint64_t sum_leader=0;

        for(int i=0;i<dataOfClient_leader.size();i++)
        {
          for(int j=0;j<dataOfServer_leader.size();j++)
          {
            for(int k=0;k<dataOfServer_leader[j].size();k++)
            {
              if(dataOfClient_leader[i]==dataOfServer_leader[j][k])
                sum_leader += allShare_leader[j][k]; 
            }
          }
        }
        std::cout<<"sum_leader is : "<<sum_leader<<"\n";
     

        sock->Send(&sum_leader, sizeof(sum_leader)); 
        context.timings.search=search_leader.end();
      }
    }
} else if (context.psm_type == PsiAnalyticsContext::SMPAQ1) {
  if(isLeader)
      {
        uint64_t len;
        sock->Receive(&len,sizeof(uint64_t));

        // n g
        char* str=new char[len];
        sock->Receive(str,sizeof(char)*len);
        
        std::string ng_str(str);

        delete[] str;
        uint64_t i=ng_str.find('|');
        const std::string& n_str=ng_str.substr(0,i);
        const std::string& g_str=ng_str.substr(i+1,std::string::npos);
        ng[0]=NTL::conv<NTL::ZZ>(n_str.c_str());
        ng[1]=NTL::conv<NTL::ZZ>(g_str.c_str());
      }

      if(!isCenter)
      {
        #if 0

        std::cout<<"Server "<<to_string(context.index)<<" flag:"<<to_string(Sf2_ng++)<<"\n";

        #else

        Sf2_Ng++;

        #endif
      }
      
        
      waitFor(Sf2_Ng,[=](){},isCenter,context.n-1);

      #if 0

      std::cout<<"Server "<<to_string(context.index)<<" ng:"<<ng[0]<<" "<<ng[1]<<"\n";

      #endif
      
      std::vector<NTL::ZZ> encryptData;
      #if 1

      if(!fileExists("Server_Data_EncryptData_"+to_string(context.index)+".csv"))
      {
        std::vector<NTL::ZZ> numbers=Paillier::numbers(context.sneles);
        encryptData=Paillier::encrypt(numbers,ng[0],ng[1]);
        std::vector<std::vector<NTL::ZZ>> tmp;
        tmp.push_back(encryptData);

        writeToCSV(tmp,"Server_Data_EncryptData_"+to_string(context.index)+".csv");
      }
      else
      {
        const auto& data=readZZFromCSV("Server_Data_EncryptData_"+to_string(context.index)+".csv");
        encryptData=data[0];
      }

      #else

      std::vector<NTL::ZZ> numbers=Paillier::numbers(context.sneles);
      encryptData=Paillier::encrypt(numbers,ng[0],ng[1]);
      std::vector<std::vector<NTL::ZZ>> tmp;
      tmp.push_back(encryptData);

      #ifdef DEBUG

      writeToCSV(tmp,"Server_Data_EncryptData_"+to_string(context.index)+".csv");

      #endif

      #endif

      Timer addtime;
      serverData.add(encryptData, context.index);
      context.timings.addtime=addtime.end();
      context.timings.encrypt=encryptTime.end();
      Sf2++;
      std::vector<std::vector<NTL::ZZ>> dataOfPsm2;

       waitFor(
        Sf2,
        [&]() {
          dataOfPsm2=serverData.data();
        },
        isLeader, context.n);

        Timer wholeoprf;
    psmTime.start();

    if(!isCenter)
    {
      auto oprf_value = ot_sender(simulated_simple_table_1, chl, context);

      auto raw_data=fromServerOprfData(oprf_value,context.sneles,1);

      #ifdef DEBUG

      // write to file

      writeToCSV(raw_data,"Server_Oprf1_"+to_string(context.index)+".csv");

      #endif

      auto data=raw_data[0];

      serverID.add(data,context.index);

      #ifdef DEBUG

      std::cout<<"Server "<<to_string(context.index)<<" flag:"<<to_string(Sf1++)<<"\n";

      #else

      Sf1++;

      #endif
    }

    waitFor(Sf1,[&]()
    {

      serverID.add(simulated_simple_table_1[0],context.index);
    },isCenter,context.n-1);
    if(isCenter)
    {

      auto oprf_value = ot_sender(serverID.data(), chl, context, context.n);


      auto data=fromServerOprfData(oprf_value,context.sneles,context.sneles);

      #ifdef DEBUG

      writeToCSV(data,"Server_Oprf2_"+to_string(context.index)+".csv");

      #endif

      for(int i=0;i<data.size();i++)
        serverID.add(data[i],i);
    }
     else
    {
      #ifdef DEBUG

      std::cout<<"Server "<<to_string(context.index)<<" flag:"<<to_string(Sf2++)<<"\n";

      #else

      Sf2++;

      #endif
    }
    waitFor(Sf2,[=]()
    {
      // 
    },isCenter,context.n-1);
    if(isLeader)
    {
      std::vector<uint64_t> dataOfClient(context.n*context.cnbins);
      std::vector<std::vector<uint64_t>> dataOfServer=serverID.data();

      sock->Receive(dataOfClient.data(),sizeof(uint64_t)*context.cnbins*context.n);

      #ifdef DEBUG

      writeToCSV(dataOfServer,"Server_All_ID.csv");
      writeToCSV(serverData.data(),"Server_All_EncryptData.csv");
      std::vector<std::vector<uint64_t>> tmp;
      tmp.push_back(dataOfClient);
      writeToCSV(tmp,"Client_All_ID.csv");

      #endif


      if(context.psm_type == PsiAnalyticsContext::SMPAQ2)
    
      {
        Timer search;
        uint64_t count=0;

        for(int i=0;i<dataOfClient.size();i++)
        {
          for(int j=0;j<dataOfServer.size();j++)
          {
            for(int k=0;k<dataOfServer[j].size();k++)
            {
              if(dataOfClient[i]==dataOfServer[j][k])
                count++;
            }
          }
        }
        std::cout<<"Count : "<<count<<"\n";
        uint64_t num=0;

        if(count>0&&count<=context.g)
          num=1;
        else if(count>context.g)
          num=2;

        sock->Send(&num,sizeof(num));
        context.timings.search=search.end();
      }
      else if(context.psm_type == PsiAnalyticsContext::SMPAQ1)
      {
        Timer search;

        NTL::ZZ sum=Paillier::encryptNumber(NTL::ZZ(0), ng[0], ng[1]);

        for(int i=0;i<dataOfClient.size();i++)
        {
          for(int j=0;j<dataOfServer.size();j++)
          {
            for(int k=0;k<dataOfServer[j].size();k++)
            {
              if(dataOfClient[i]==dataOfServer[j][k])
              {
                sum = (sum * dataOfPsm2[j][k]) % (ng[0] * ng[0]);
              }
            }
          }
        }

        #if 0

        std::cout<<"Sum : "<<sum<<"\n";

        #endif

          std::stringstream ss;
          ss << sum;

          std::string str = ss.str();
          uint64_t len = str.size();

          sock->Send(&len, sizeof(uint64_t));
          sock->Send(str.c_str(), sizeof(char) * len);

          context.timings.search = search.end();
          //std::cout << "Search Time : " << context.timings.search << "\n";
      }
    }
}
  }
  context.timings.psm=psmTime.end();
  context.timings.total=totalTime.end();
}



std::unique_ptr<CSocket> EstablishConnection(const std::string &address, uint16_t port,
                                         e_role role){
  std::unique_ptr<CSocket> socket;
  if (role == SERVER) {
    socket = Listen(address.c_str(), port);
  } else {
    socket = Connect(address.c_str(), port);
  }
  assert(socket);
  return socket;
}

/*
 * Print Timings
 */
void PrintTimings(const PsiAnalyticsContext &context)
{

  if(context.role==SERVER)
  {
    std::cout << "Time for vrf " << context.timings.vrf << " ms\n";
  }


    std::cout << "Time for OPRF1 " << context.timings.oprf1 << " ms\n";
    std::cout << "Time for OPRF2 " << context.timings.oprf2 << " ms\n";

  
  std::cout << "Time for hint computation " << context.timings.hint_computation << " ms\n";

  if(context.psm_type == PsiAnalyticsContext::SMPAQ1)
  {
    if(context.role==SERVER)
    {
      std::cout << "Time for encrypt " << context.timings.encrypt << " ms\n";
    }
    else
    {
      std::cout << "Time for decrypt " << context.timings.decrypt << " ms\n";
    }
  }
  

  std::cout << "Timing for PSM " << context.timings.psm<< " ms\n";
  std::cout << "Total runtime " << context.timings.total<< " ms\n";
  std::cout << "Total runtime w/o base OTs:" << context.timings.totalWithoutOT<< " ms\n";
}

/*
 * Clear communication counts for new execution
 */

void ResetCommunication(std::unique_ptr<CSocket> &sock, osuCrypto::Channel &chl, PsiAnalyticsContext &context) {
    chl.resetStats();
    sock->ResetSndCnt();
    sock->ResetRcvCnt();
}

void ResetCommunication(std::unique_ptr<CSocket> &sock, osuCrypto::Channel &chl, sci::NetIO* ioArr[2], PsiAnalyticsContext &context) {
    chl.resetStats();
    sock->ResetSndCnt();
    sock->ResetRcvCnt();
    context.sci_io_start.resize(2);
		for(int i=0; i<2; i++) {
				context.sci_io_start[i] = ioArr[i]->counter;
		}
}

/*
 * Measure communication
 */
void AccumulateCommunicationPSI(std::unique_ptr<CSocket> &sock, osuCrypto::Channel &chl, PsiAnalyticsContext &context) {

  context.sentBytesOPRF = chl.getTotalDataSent();
  context.recvBytesOPRF = chl.getTotalDataRecv();

  context.sentBytesHint = sock->getSndCnt();
  context.recvBytesHint = sock->getRcvCnt();


  if(context.role==SERVER)
  {
   if(context.psm_type==context.SMPAQ2){
    if(context.index!=context.n-1)
    {
      context.sentBytesHint+=sizeof(osuCrypto::block)*2*context.sneles;
      context.sentBytesHint+=sizeof(uint64_t)*2*context.sneles;
   
      if(context.psm_type==context.SMPAQ1)
        context.sentBytesHint+=sizeof(char) * 528*context.sneles;
      if (context.index == 0)
      {
        context.sentBytesHint-=sizeof(osuCrypto::block)*context.sneles;
        context.sentBytesHint-=sizeof(uint64_t)*context.sneles;
        context.sentBytesHint+=sizeof(osuCrypto::block)*context.sneles*context.n;
        context.sentBytesHint+=sizeof(uint64_t)*context.sneles*context.n;

        context.recvBytesHint+= sizeof(osuCrypto::block) * context.sneles * context.n-1;
        context.recvBytesHint+= sizeof(uint64_t) * context.sneles * context.n-1;
        context.recvBytesHint+= sizeof(osuCrypto::block) * context.sneles * context.n;
        context.recvBytesHint+= sizeof(uint64_t) * context.sneles * context.n;
      }
    }
    else 
    {
      context.sentBytesHint+=sizeof(osuCrypto::block)*context.sneles;
      context.sentBytesHint+=sizeof(uint64_t)*context.sneles;
      context.sentBytesHint+=sizeof(osuCrypto::block)*context.sneles*context.n;
      context.recvBytesHint+= sizeof(osuCrypto::block) * context.sneles * context.n-1;
      context.recvBytesHint+= sizeof(uint64_t) * context.sneles * context.n-1;
      context.recvBytesHint+= sizeof(osuCrypto::block) * context.sneles * context.n;
      context.recvBytesHint+= sizeof(uint64_t) * context.sneles * context.n;
    }
   }else if(context.psm_type==context.SMPAQ1){
    if(context.index!=context.n-1)
    {
      context.sentBytesHint+=sizeof(osuCrypto::block)*context.sneles;
  
      if(context.psm_type==context.SMPAQ1)
        context.sentBytesHint+=sizeof(char) * context.bitlen*context.sneles;
    
      if (context.index == 0)
      {
        context.recvBytesHint += sizeof(osuCrypto::block) * context.sneles * context.n;

        if (context.psm_type == context.SMPAQ1)
        {
          context.recvBytesHint += sizeof(char) * context.bitlen* context.sneles * context.n;
          context.recvBytesHint -= sizeof(uint64_t) * 2 + sizeof(char) * (2*context.bitlen+1);
        }
      }
    }
    else 
    {
      context.recvBytesHint+=sizeof(osuCrypto::block)*context.sneles*(context.n-1);
      context.sentBytesHint+=sizeof(osuCrypto::block)*context.sneles*context.n;
      if(context.psm_type==context.SMPAQ1)
      {
        context.recvBytesHint+=sizeof(char) * context.bitlen*context.sneles*(context.n-1);
        context.sentBytesHint+=sizeof(char) * context.bitlen*context.sneles*context.n;
      }
    }
    }

  }
  else
  {

   if(context.psm_type==context.SMPAQ2){
  
   }else if(context.psm_type==context.SMPAQ1){
     if(context.index==0 && context.psm_type==context.SMPAQ1)
    {
      context.sentBytesHint -= sizeof(uint64_t) * 2 + sizeof(char) * (2*context.bitlen+1);
    }


   }

   
  }

  context.sentBytesSCI = 0;
  context.recvBytesSCI = 0;

  //Send SCI Communication
  if (context.role == CLIENT) {
    sock->Receive(&context.recvBytesSCI, sizeof(uint64_t));
    sock->Send(&context.sentBytesSCI, sizeof(uint64_t));
  } else {
    sock->Send(&context.sentBytesSCI, sizeof(uint64_t));
    sock->Receive(&context.recvBytesSCI, sizeof(uint64_t));
  }
}



/*
 * Print communication
 */
void PrintCommunication(PsiAnalyticsContext &context) {
  /* if (context.index==context.n-1 || context.index==0 || context.index==1)
  {
  context.sentBytes = context.sentBytesOPRF + context.sentBytesHint + context.sentBytesSCI;
  context.recvBytes = context.recvBytesOPRF + context.recvBytesHint + context.recvBytesSCI;
  std::cout<<(context.role==SERVER?"Server":"Client")<<" "<<to_string(context.index)<< ": Communication Statistics: "<<std::endl;
  double sentinMB, recvinMB;
  sentinMB = context.sentBytesOPRF/((1.0*(1ULL<<20)));
  recvinMB = context.recvBytesOPRF/((1.0*(1ULL<<20)));
  std::cout<<(context.role==SERVER?"Server":"Client")<<" "<<to_string(context.index)<< ": Sent Data OPRF (MB): "<<sentinMB<<std::endl;
  std::cout<<(context.role==SERVER?"Server":"Client")<<" "<<to_string(context.index)<< ": Received Data OPRF (MB): "<<recvinMB<<std::endl;

  sentinMB = context.sentBytesHint/((1.0*(1ULL<<20)));
  recvinMB = context.recvBytesHint/((1.0*(1ULL<<20)));
  std::cout<<(context.role==SERVER?"Server":"Client")<<" "<<to_string(context.index)<< ": Sent Data Hint (MB): "<<sentinMB<<std::endl;
  std::cout<<(context.role==SERVER?"Server":"Client")<<" "<<to_string(context.index)<< ": Received Data Hint (MB): "<<recvinMB<<std::endl;

  //sentinMB = context.sentBytesSCI/((1.0*(1ULL<<20)));
  //recvinMB = context.recvBytesSCI/((1.0*(1ULL<<20)));
  //std::cout<<(context.role==SERVER?"Server":"Client")<<" "<<to_string(context.index)<< ": Sent Data CryptFlow2 (MB): "<<sentinMB<<std::endl;
  //std::cout<<(context.role==SERVER?"Server":"Client")<<" "<<to_string(context.index)<< ": Received Data CryptFlow2 (MB): "<<recvinMB<<std::endl; 

  sentinMB = context.sentBytes/((1.0*(1ULL<<20)));
  recvinMB = context.recvBytes/((1.0*(1ULL<<20)));
  std::cout<<(context.role==SERVER?"Server":"Client")<<" "<<to_string(context.index)<< ": Total Sent Data (MB): "<<sentinMB<<std::endl;
  std::cout<<(context.role==SERVER?"Server":"Client")<<" "<<to_string(context.index)<< ": Total Received Data (MB): "<<recvinMB<<std::endl;

 if (context.role == CLIENT) {

      static double sent_gen = 0, recv_gen = 0;      // index==1
      static double sent_lea = 0, recv_lea = 0;      // index==0 (leader)
      static double sent_ctr = 0, recv_ctr = 0;      // index==n-1 (center)


      if (context.index == 1) {
        sent_gen = sentinMB;
        recv_gen = recvinMB;
      }
      else if (context.index == 0) {
        sent_lea = sentinMB;
        recv_lea = recvinMB;
      }
      else  {
        sent_ctr = sentinMB;
        recv_ctr = recvinMB;
      }
      if (context.index == context.n - 1) {
        double totalSentAllClients = sent_gen * (context.n - 2)
                                     + sent_lea
                                     + sent_ctr*2;
        double totalRecvAllClients = recv_gen * (context.n - 2)
                                     + recv_lea
                                     + recv_ctr*2;

        std::cout << "XXXX client Total Sent Data (MB): "
                  << totalSentAllClients << std::endl;
        std::cout << "XXXX client Total Received Data (MB): "
                  << totalRecvAllClients << std::endl;
      }
    }
  
  } */
}
}
