// ARM oracle fixture only. Unexpected fallback is independently observable.
extern "C" {
int native_math_test_enabled=1;
int sm64ds_fresh_native_enabled(){return native_math_test_enabled;}
char* getenv(const char*){return nullptr;}
int __real_LenVec3(const int*){return -777;}
int __real_func_02053274(const int*,const int*){return -778;}
int __real__ZN4cstd4sqrtEy(unsigned long long){return -779;}
int __real_func_02053644(const short*){return -780;}
int __real_func_02049018(const int*){return -781;}
}
