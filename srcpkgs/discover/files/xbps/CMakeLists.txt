set(xbps-backend_SRCS
    XbpsBackend.cpp
    XbpsBackend.h
    XbpsResource.cpp
    XbpsResource.h
    XbpsSourcesBackend.cpp
    XbpsSourcesBackend.h
    XbpsTransaction.cpp
    XbpsTransaction.h
    XbpsUpdater.cpp
    XbpsUpdater.h
)

kcoreaddons_add_plugin(xbps-backend SOURCES ${xbps-backend_SRCS} INSTALL_NAMESPACE "discover")

target_link_libraries(xbps-backend
    Qt::Core
    Qt::Widgets
    Qt::Concurrent
    KF6::CoreAddons
    KF6::ConfigCore
    Discover::Common
)
