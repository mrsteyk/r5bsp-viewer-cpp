target_sources(r5bsp PRIVATE
    imgui.cpp
    imgui_widgets.cpp
    imgui_tables.cpp
    imgui_draw.cpp
    imgui_demo.cpp
    # ---
    backends/imgui_impl_glfw.cpp
    backends/imgui_impl_opengl3.cpp
)