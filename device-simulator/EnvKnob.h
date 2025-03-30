///
/// handle environment knobs
/// 2021 Tong Zhang <ztong0001@gmail.com>
/// 2022 Yilun Wu<yiluwu@cs.stonybrook.edu>
///
#ifndef _ENV_KNOB_
#define _ENV_KNOB_
#include <assert.h>
#include <stdlib.h>
#include <string>
#include <stdint.h>
#include <iostream>

class EnvKnob {
public:
  EnvKnob(const std::string name) : name(name), value("") {
    std::cerr << "enter EnvKnob constructor" << std::endl;
    std::cerr << "name = " << name << std::endl;
    auto *val = getenv(name.c_str());
    std::cerr << "val = " << val << std::endl;

    if (val == nullptr)
      return;
    value = std::string(val);
  }
  ~EnvKnob(){};
  bool isPresented() { return value.length() != 0; }
  bool isSet() {
    if (value.length() == 0)
      return false;
    if (value == "true")
      return true;
    if ((value == "false") || (value == "0"))
      return false;
    return true;
  };
  int getIntValue() {
    assert(value.length() != 0);
    return atoi(value.c_str());
  };
  uint16_t getUInt16Value() {
    assert(value.length() != 0);
    return atoi(value.c_str());
  }
  uint32_t getUInt32Value() {
    assert(value.length() != 0);
    return atoi(value.c_str());
  }
  std::string getStringValue() { return value; }

private:
  const std::string name;
  std::string value;
};
#endif
