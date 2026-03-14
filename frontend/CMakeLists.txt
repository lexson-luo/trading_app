add_executable(hf_client
    main.cpp
    api_client.cpp
    ui/login_panel.cpp
    ui/sidebar.cpp
    ui/dashboard_panel.cpp
    ui/backtest_panel.cpp
    ui/trading_panel.cpp
    ui/risk_panel.cpp
)

target_include_directories(hf_client PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_SOURCE_DIR}/common
)

target_link_libraries(hf_client PRIVATE
    hf_common
    httplib
    imgui
    implot
    GLEW::GLEW
    OpenGL::GL
    SDL3::SDL3-static
    nlohmann_json::nlohmann_json
    Threads::Threads
    ${CMAKE_DL_LIBS}
)

# Low-latency: strip debug info in Release, keep frame pointer for profiling
target_compile_options(hf_client PRIVATE
    $<$<CONFIG:Release>:-fno-omit-frame-pointer>
)
