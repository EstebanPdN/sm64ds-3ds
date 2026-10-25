#pragma once
namespace enhance {
// A held Start can reveal the hint, but never confirm its own request.
struct IntroSkip {
 bool active=false, hint=false, released=false;
 void begin(){active=true;hint=false;released=false;}
 void end(){active=hint=released=false;}
 bool update(bool held,bool pressed){
  if(!active)return false;
  if(!held)released=true;
  if(!pressed)return false;
  if(!hint){hint=true;released=false;return false;}
  if(!released)return false;
  end();return true;
 }
};
}
