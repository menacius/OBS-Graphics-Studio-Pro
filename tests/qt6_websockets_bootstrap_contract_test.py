from pathlib import Path
import re
root = Path(__file__).resolve().parents[1]
cmake = (root / 'CMakeLists.txt').read_text(encoding='utf-8')
bootstrap = (root / 'cmake' / 'BootstrapQtWebSockets.cmake').read_text(encoding='utf-8')
version = re.search(r'set\(OBS_BGS_DEVELOPMENT_VERSION \"(\d+)\"\)', cmake)
assert version and int(version.group(1)) >= 110
assert 'include(ExternalProject)' in bootstrap
assert 'ExternalProject_Add(obs_bgs_qtwebsockets_external' in bootstrap
assert 'FetchContent_MakeAvailable' not in bootstrap
assert 'FetchContent_MakeAvailable(qtwebsockets)' not in bootstrap
assert 'GIT_TAG "${_qtws_tag}"' in bootstrap
assert 'CMAKE_PREFIX_PATH=${_qt6_prefix}' in bootstrap
assert 'QT_BUILD_EXAMPLES=OFF' in bootstrap
assert 'QT_BUILD_TESTS=OFF' in bootstrap
assert 'BUILD_BYPRODUCTS "${_qtws_library}"' in bootstrap
assert 'add_library(Qt6::WebSockets ALIAS obs_bgs_qt6_websockets)' in bootstrap
assert 'add_dependencies(obs_bgs_qt6_websockets obs_bgs_qtwebsockets_external)' in bootstrap
print('Qt6 WebSockets isolated bootstrap contract passed')
