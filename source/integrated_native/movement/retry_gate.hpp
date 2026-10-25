#pragma once
#include <array>
#include <cstdint>
namespace native_motion {
// Expected DS terrain delegation is not a core fault. Avoid recreating a
// solver every stationary update; DS gameplay continues during this delay.
// Movement/state changes and fresh logical actions retry immediately. The
// bounded retry also sees moving geometry while the actor remains stationary.
class RetryGate {
public:
 using Signature=std::array<std::int64_t,6>;
 void clear(){remaining_=0;}
 void defer(const Signature& signature){signature_=signature;remaining_=15;}
 bool ready(const Signature& signature,bool action){
  if(action || signature!=signature_){clear();return true;}
  if(remaining_){--remaining_;return false;}
  return true;
 }
private:
 Signature signature_{};unsigned remaining_=0;
};
}
