project(MSVCManager)

find_package(KDE4 4.2.0 REQUIRED)

find_package(KDevPlatform 0.9.91 REQUIRED)

kdevplatform_install_kross_plugin( kdevmsvcmanager.desktop KDevMSVCManager )

