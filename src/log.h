#pragma once
#include <source_location>
#include <string>
#include <ostream>

#define TRACE_CALL() Log::TraceFunction TRACE##__COUNTER__
#define ERROR Log::log(Log::LEVEL_ERROR)
#define WARNING Log::log(Log::LEVEL_WARNING)
#define INFO Log::log(Log::LEVEL_INFO)
#define DEBUG Log::log(Log::LEVEL_DEBUG)
#define RAW std::cout

namespace Log
{
    enum
    {
        LEVEL_ERROR = 0,
        LEVEL_WARNING,
        LEVEL_INFO,
        LEVEL_DEBUG
    };

    std::ostream log(int requested, const std::source_location loc = std::source_location::current());

    class TraceFunction
    {
    public:
        TraceFunction(const std::source_location location =
                          std::source_location::current())
            : function_name_(location.function_name())
        {
            DEBUG << "Entering " << function_name_ << std::endl;
        }
        ~TraceFunction()
        {
            DEBUG << "Leaving " << function_name_ << std::endl;
        }

    protected:
        std::string function_name_;
    };

}
