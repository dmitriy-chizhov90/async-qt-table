cmake_minimum_required(VERSION 3.18.4)


if (NOT NTPRO_ENABLE_UITESTS)
    return()
endif()

enable_testing()

add_subdirectory(utils)

add_custom_target(QtGuiDataModelTests)

function(DataModelTest)
    set(options)
    set(oneValueArgs NAME PATH)
    set(multiValueArgs CUSTOM_SOURCES)
    cmake_parse_arguments(DATAMODEL_TEST "${options}" "${oneValueArgs}" "${multiValueArgs}" ${ARGN})

    SingleTargetTest(
        NAME ${DATAMODEL_TEST_NAME}
        PATH ${DATAMODEL_TEST_PATH}
        CUSTOM_SOURCES ${DATAMODEL_TEST_CUSTOM_SOURCES}
        LIBRIARIES QtGuiDataEngineTestUtilsLib QtGuiDataModelTestUtilsLib
    )
    add_dependencies(QtGuiDataModelTests ${DATAMODEL_TEST_NAME})
endfunction()

# Custom tests
DataModelTest(NAME TableModelTest PATH TableModel)
