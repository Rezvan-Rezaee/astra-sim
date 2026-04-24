/** @file
 *
 * Basic types and utilities.
 */

#pragma once

#include <cstddef>
#include <stdexcept>
#include <string>

//-- Basic utilities
#define UNUSED(expr)  \
    do                \
    {                 \
        (void)(expr); \
    } while (0)

//-- Basic types and constants
using addr_t = size_t;
constexpr addr_t INVALID_ADDRESS = -1;

using clockCycle_t = size_t;

/** @brief Print an ERROR message and return the passed-in error code. */
int errlog(const std::string& message, int errorCode = -1);

/** @brief Print an WARNING message and return the passed-in error code. */
int warnlog(const std::string& message, int warningCode = 1);

/** @brief Determine if input integer is a power of 2. */
bool isPowerOf2(unsigned int x);

/** @brief Return the log2 value of the input integer.
 * @param x Input integer from which we compute the log2() function.
 * @return The log2() value if input "x" is a power of 2, else return -1.
 */
int integerLog2(unsigned int x);

/** @brief Return a random integer in the range [minValue..maxValue]. */
int getRandomNumber(int minValue, int maxValue);

/** @brief Change the seed of the random number generator. */
void changeRandomSeed();

/** @brief Set the seed of the random number generator. */
void setRandomSeed(unsigned int seed);

/** @brief Return the number of decimal digits required to print the given input. */
int getNbrOfDecimalDigits(int x);

/** @brief Return a string representing the HEX value of the input integer
 * @param value Value to display
 * @param displayWidth The minimum width (in characters) for the resulting string.
 * @param showBasePrefix Boolean specifying if we show the HEX prefix ("0x")
 * @return The string representing the input value.
 */
std::string
integerToHexString(const unsigned long value, size_t displayWidth, bool showBasePrefix = true);

/** @brief Return a string displayin the address held by a pointer. */
std::string pointerToHexString(const void* pointer);

//-- Assertion
#ifdef ASSERT
    // Override the macro if it is defined
    #undef ASSERT
#endif

#ifndef NDEBUG
    // #define USE_STD_TERMINATE_
    #if defined(USE_STD_TERMINATE_)
        // Using std::terminate() to exit the program
        #define ASSERT(condition)                                                      \
            do                                                                         \
            {                                                                          \
                if (!(condition))                                                      \
                {                                                                      \
                    fprintf(stderr,                                                    \
                            "ASSERT! '" #condition "' failed in %s(), %s, line %d.\n", \
                            __func__,                                                  \
                            __FILE__,                                                  \
                            __LINE__);                                                 \
                    std::terminate();                                                  \
                }                                                                      \
            } while (false)

        #define ASSERT_PRINT(condition, message, args...)                                     \
            do                                                                                \
            {                                                                                 \
                if (!(condition))                                                             \
                {                                                                             \
                    fprintf(stderr,                                                           \
                            "ASSERT! '" #condition "' failed in %s(), %s, line %d.  " message \
                            "\n",                                                             \
                            __func__,                                                         \
                            __FILE__,                                                         \
                            __LINE__,                                                         \
                            ##args);                                                          \
                    std::terminate();                                                         \
                }                                                                             \
            } while (false)
    #else
    // Throwing an exception to exit the program
        #define ASSERT(condition)                                                      \
            do                                                                         \
            {                                                                          \
                if (!(condition))                                                      \
                {                                                                      \
                    fprintf(stderr,                                                    \
                            "ASSERT! '" #condition "' failed in %s(), %s, line %d.\n", \
                            __func__,                                                  \
                            __FILE__,                                                  \
                            __LINE__);                                                 \
                    throw std::runtime_error("error");                                 \
                }                                                                      \
            } while (false)

        #define ASSERT_PRINT(condition, message, args...)                                     \
            do                                                                                \
            {                                                                                 \
                if (!(condition))                                                             \
                {                                                                             \
                    fprintf(stderr,                                                           \
                            "ASSERT! '" #condition "' failed in %s(), %s, line %d.  " message \
                            "\n",                                                             \
                            __func__,                                                         \
                            __FILE__,                                                         \
                            __LINE__,                                                         \
                            ##args);                                                          \
                    throw std::runtime_error("error");                                        \
                }                                                                             \
            } while (false)

    #endif
#else
    #define ASSERT(condition)
    #define ASSERT_PRINT(condition, message, args...)
#endif
