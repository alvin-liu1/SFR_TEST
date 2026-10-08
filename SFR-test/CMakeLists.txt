cmake_minimum_required(VERSION 3.15)
project(SFR-test LANGUAGES CXX)

# 使用 C++17 (std::filesystem 需要)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# 查找 OpenCV
set(OpenCV_DIR "D:/SoftWare/opencvInstall/opencv/build/x64/mingw/lib")
find_package(OpenCV REQUIRED)

# 源文件
set(SOURCES
    main.cpp
    sfr.cpp
    RrConfig.cpp
    AoiManager.cpp
    ResultLogger.cpp
    SfrTestRunner.cpp
)

# 头文件
set(HEADERS
    sfr.h
    RrConfig.h
    AoiManager.h
    ResultLogger.h
    SfrTestRunner.h
)

# 生成可执行文件
add_executable(SFR-test ${SOURCES} ${HEADERS})

# 链接 OpenCV
target_link_libraries(SFR-test PRIVATE ${OpenCV_LIBS})

# 编译选项
target_include_directories(SFR-test PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})

# Windows 特定定义
if(WIN32)
    target_compile_definitions(SFR-test PRIVATE _CRT_SECURE_NO_WARNINGS)
endif()

# MinGW 可能需要额外链接 stdc++fs 以支持 std::filesystem
if(MINGW)
    target_link_libraries(SFR-test PRIVATE stdc++fs)
endif()
