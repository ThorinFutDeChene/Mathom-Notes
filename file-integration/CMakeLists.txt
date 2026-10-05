kcoreaddons_add_plugin(
    basketthumbcreator
    SOURCES
        basketthumbcreator.cpp
        basketthumbcreator.h
    INSTALL_NAMESPACE
        "kf6/thumbcreator"
)

target_link_libraries(
    basketthumbcreator
    PRIVATE
        Qt::Core
        Qt::Gui
        KF6::KIOGui
)

install(
    FILES basket.xml
    DESTINATION ${KDE_INSTALL_MIMEDIR}
)
