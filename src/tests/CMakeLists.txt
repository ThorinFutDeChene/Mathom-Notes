########### next target ###############

include(ECMMarkAsTest)
include(ECMAddTests)

find_package(Qt6 ${REQUIRED_QT_VERSION} CONFIG REQUIRED Test)

set(BASKET_TEST_SRC
    notetest.cpp
    basketviewtest.cpp
    toolstest.cpp
    archivetest.cpp
    backuptest.cpp
    notecontentrenametest.cpp
    notefactorytest.cpp
    secondarylanguagetest.cpp
    secondarylanguageenginetest.cpp
    secondarylanguagevalidatortest.cpp
    mathsymbolenginetest.cpp
    mathscientificexpressiontest.cpp
)

ecm_add_tests(${BASKET_TEST_SRC} LINK_LIBRARIES LibBasket Qt::Test)

# Native scanning export has no dependency on physical scanner hardware.
ecm_add_test(mathomscanexporttest.cpp ../mathomscanexport.cpp
    TEST_NAME mathomscanexporttest
    LINK_LIBRARIES Qt::Test Qt::Gui Qt::Widgets)
