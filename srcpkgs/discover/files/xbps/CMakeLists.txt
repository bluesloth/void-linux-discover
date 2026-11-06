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

# Tray icon / "Updates available" notification. DiscoverNotifier is a separate
# process that never touches the backend API: it only loads plugins from
# <plugindir>/discover-notifier/ whose IID matches DISCOVER_NOTIFIER_IID, so
# without this module pending xbps updates never reach the tray.
kcoreaddons_add_plugin(XbpsNotifier SOURCES XbpsNotifier.cpp INSTALL_NAMESPACE "discover-notifier")

target_link_libraries(XbpsNotifier
    PRIVATE
        Discover::Notifiers
        Qt::Core
        Qt::Concurrent
)

# libDiscoverNotifiers.so lives beside the Discover plugins, not in the
# notifier plugin directory, so the RPATH has to point at it.
set_target_properties(XbpsNotifier PROPERTIES INSTALL_RPATH ${CMAKE_INSTALL_FULL_LIBDIR}/plasma-discover)
