add_executable(custom_benchmarks_main custom_benchmarks_main.cpp)

if (NOT MOTION_BUILD_BOOST_FROM_SOURCES)
    find_package(Boost
            COMPONENTS
            program_options
            REQUIRED)
endif ()

target_link_libraries(custom_benchmarks_main
        MOTION::motion
        Boost::program_options
        )
