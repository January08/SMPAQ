// Original Work ots.cpp copyright (c) Oleksandr Tkachenko
// Modified Work block_op_ots.cpp copyright (c) 2021 Microsoft Research
//
// \file ots.cpp
// \author Oleksandr Tkachenko
// \email tkachenko@encrypto.cs.tu-darmstadt.de
// \organization Cryptography and Privacy Engineering Group (ENCRYPTO)
// \TU Darmstadt, Computer Science department
//
// \copyright The MIT License. Copyright Oleksandr Tkachenko
//
// Permission is hereby granted, free of charge, to any person obtaining
// a copy of this software and associated documentation files (the "Software"),
// to deal in the Software without restriction, including without limitation
// the rights to use, copy, modify, merge, publish, distribute, sublicense,
// and/or sell copies of the Software, and to permit persons to whom the Software
// is furnished to do so, subject to the following conditions:
// The above copyright notice and this permission notice shall be included in all
// copies or substantial portions of the Software.
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED,
// INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS FOR
// A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT
// HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION
// OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE
// OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
//
// Modified by Akash Shah

#include "ots.h"

#include "common/constants.h"
#include "common/config.h"
#include <iomanip>
#include <iostream>
#include <vector>
#include <sstream>
#include <fstream>

using milliseconds_ratio = std::ratio<1, 1000>;
using duration_millis = std::chrono::duration<double, milliseconds_ratio>;

namespace ENCRYPTO {

// Client
  std::vector<osuCrypto::block> ot_receiver(const std::vector<std::uint64_t> &inputs, osuCrypto::Channel& recvChl,
                                       ENCRYPTO::PsiAnalyticsContext &context,std::size_t numOTs) {
  osuCrypto::PRNG prng(_mm_set_epi32(4253233465, 334565, 0, 235));

  osuCrypto::KkrtNcoOtReceiver recv;

  // get up the parameters and get some information back.
  //  1) false = semi-honest
  //  2) 40  =  statistical security param.
  //  3) numOTs = number of OTs that we will perform
  recv.configure(false, 40, symsecbits);

  const auto baseots_start_time = std::chrono::system_clock::now();
  // the number of base OT that need to be done
  osuCrypto::u64 baseCount = recv.getBaseOTCount();
  std::vector<osuCrypto::block> baseRecv(baseCount);

  std::vector<std::array<osuCrypto::block, 2>> baseSend(baseCount);

  osuCrypto::DefaultBaseOT baseOTs;
  baseOTs.send(baseSend, prng, recvChl, 1);
  recv.setBaseOts(baseSend);
  const auto baseots_end_time = std::chrono::system_clock::now();
  const duration_millis baseOTs_duration = baseots_end_time - baseots_start_time;
  // std::cout << "Client Base OTs time: " << baseOTs_duration.count() << " ms" << std::endl;

  const auto OPRF_start_time = std::chrono::system_clock::now();
  recv.init(numOTs, prng, recvChl);

  std::vector<osuCrypto::block> blocks(numOTs), receiver_encoding(numOTs);

  for (auto i = 0ull; i < inputs.size(); ++i) {
    blocks.at(i) = osuCrypto::toBlock(inputs[i]);
  }

  //const auto OPRF_start_time = std::chrono::system_clock::now();

  for (auto k = 0ull; k < numOTs && k < inputs.size(); ++k) {
    recv.encode(k, &blocks.at(k), reinterpret_cast<uint8_t *>(&receiver_encoding.at(k)),
                sizeof(osuCrypto::block));
  }

  recv.sendCorrection(recvChl, numOTs);

  const auto OPRF_end_time = std::chrono::system_clock::now();
  const duration_millis OPRF_duration = OPRF_end_time - OPRF_start_time;

  if(numOTs!=context.n)
  {
    context.timings.oprf1 = OPRF_duration.count();
    context.timings.base_ots_libote = baseOTs_duration.count();
    //std::cout << "The client " <<std::to_string(context.index) <<" base ot time is " << baseOTs_duration.count() << " ms" << std::endl;
  }
    
  else{
    context.timings.oprf2 = OPRF_duration.count();
    context.timings.base_ots_libote2 = baseOTs_duration.count();
    //std::cout << "The client " <<std::to_string(context.index) <<" base ot time is " << baseOTs_duration.count() << " ms" << std::endl;
  }
  return receiver_encoding;
}

// Server
  std::vector<std::vector<osuCrypto::block>> ot_sender(
  const std::vector<std::vector<std::uint64_t>> &inputs, osuCrypto::Channel& sendChl, ENCRYPTO::PsiAnalyticsContext &context,std::size_t numOTs) {
  osuCrypto::PRNG prng(_mm_set_epi32(4253465, 3434565, 234435, 23987025));
  osuCrypto::KkrtNcoOtSender sender;
  // get up the parameters and get some information back.
  //  1) false = semi-honest
  //  2) 40  =  statistical security param.
  //  3) numOTs = number of OTs that we will perform
  sender.configure(false, 40, 128);

  const auto baseots_start_time = std::chrono::system_clock::now();

  osuCrypto::u64 baseCount = sender.getBaseOTCount();
  osuCrypto::DefaultBaseOT baseOTs;
  osuCrypto::BitVector choices(baseCount);
  std::vector<osuCrypto::block> baseRecv(baseCount);
  choices.randomize(prng);

  baseOTs.receive(choices, baseRecv, prng, sendChl, 1);
  sender.setBaseOts(baseRecv, choices);
  const auto baseots_end_time = std::chrono::system_clock::now();
  const duration_millis baseOTs_duration = baseots_end_time - baseots_start_time;
  // std::cout << "Server Base OTs time: " << baseOTs_duration.count() << " ms" << std::endl;

  const auto OPRF_start_time = std::chrono::system_clock::now();
  sender.init(numOTs, prng, sendChl);

  std::vector<std::vector<osuCrypto::block>> inputs_as_blocks(numOTs), outputs_as_blocks(numOTs);
  for (auto i = 0ull; i < numOTs; ++i) {
    outputs_as_blocks.at(i).resize(inputs.at(i).size());
    for (auto &var : inputs.at(i)) {
      inputs_as_blocks.at(i).push_back(osuCrypto::toBlock(var));
    }
  }

  //const auto OPRF_start_time = std::chrono::system_clock::now();

  sender.recvCorrection(sendChl, numOTs);

  for (auto i = 0ull; i < numOTs; ++i) {
    for (auto j = 0ull; j < inputs_as_blocks.at(i).size(); ++j) {
      sender.encode(i, &inputs_as_blocks.at(i).at(j), &outputs_as_blocks.at(i).at(j),
                    sizeof(osuCrypto::block));
    }
  }

  const auto OPRF_end_time = std::chrono::system_clock::now();
  const duration_millis OPRF_duration = OPRF_end_time - OPRF_start_time;
  if(numOTs!=context.n)
  {
    context.timings.oprf1 = OPRF_duration.count();
    context.timings.base_ots_libote = baseOTs_duration.count();
    //std::cout << "The server" <<std::to_string(context.index) <<" base ot time is " << baseOTs_duration.count() << " ms" << std::endl;
  }
  
  else{
    context.timings.oprf2 = OPRF_duration.count();
    std::cout<<"The server "<<std::to_string(context.index)<<"oprf2 time is "<<OPRF_duration.count()<<" ms"<<std::endl;
    context.timings.base_ots_libote2 = baseOTs_duration.count();
    //std::cout << "The server " <<std::to_string(context.index) <<" base ot time is " << baseOTs_duration.count() << " ms" << std::endl;
  }

  return outputs_as_blocks;
}

}

