#ifndef TIMESTAMP_H
#define TIMESTAMP_H


#include <chrono>


namespace utuputki {


typedef std::chrono::system_clock::duration    Duration;
typedef std::chrono::system_clock::time_point  Timestamp;


}  // namespace utuputki


#endif  // TIMESTAMP_H
