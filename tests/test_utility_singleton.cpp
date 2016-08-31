//
// Created by Ivan Shynkarenka on 31.08.2016.
//

#include "catch.hpp"

#include "utility/singleton.h"

using namespace CppCommon;

class TestSingleton : public CppCommon::Singleton<TestSingleton>
{
   friend CppCommon::Singleton<TestSingleton>;

public:
    bool Test() { return _init; }

private:
    bool _init;
    TestSingleton() : _init(true) {}
    ~TestSingleton() { _init = false; }
};

TEST_CASE("Singleton", "[CppCommon][Utility]")
{
    REQUIRE(TestSingleton::GetInstance().Test());
}
