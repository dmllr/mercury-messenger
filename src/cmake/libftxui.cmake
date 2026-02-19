set(FTXUI_QUIET ON)
set(FTXUI_ENABLE_INSTALL OFF)
import_remote("https://github.com/ArthurSonzogni/FTXUI.git"
	TAG "v6.1.9"
	DST "${REPO_ROOT}/_imported"
	ALIAS ftxui
)

add_library(libftxui INTERFACE)
target_link_libraries(libftxui INTERFACE
	screen
	dom
	component
)
