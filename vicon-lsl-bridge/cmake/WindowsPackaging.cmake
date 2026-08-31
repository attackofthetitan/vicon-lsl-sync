function(vicon_lsl_configure_windows_gui_packaging bridge_source_dir lsl_target)
    target_link_libraries(vicon-lsl-bridge-gui PRIVATE ws2_32)
    set_target_properties(vicon-lsl-bridge-gui PROPERTIES WIN32_EXECUTABLE TRUE)

    find_program(VICON_LSL_POWERSHELL pwsh powershell)
    find_program(VICON_LSL_WINDEPLOYQT windeployqt)

    if(VICON_LSL_DEPLOY_QT_RUNTIME AND VICON_LSL_WINDEPLOYQT AND
       NOT VICON_LSL_POWERSHELL)
        message(FATAL_ERROR
            "PowerShell is required to deploy the x64 MSVC runtime after windeployqt")
    endif()

    if(VICON_LSL_DEPLOY_QT_RUNTIME AND VICON_LSL_WINDEPLOYQT AND
       VICON_LSL_POWERSHELL)
        add_custom_command(TARGET vicon-lsl-bridge-gui POST_BUILD
            COMMAND "${CMAKE_COMMAND}" -E copy_if_different
                "$<TARGET_FILE:${lsl_target}>"
                "$<TARGET_FILE_DIR:vicon-lsl-bridge-gui>/$<TARGET_FILE_NAME:${lsl_target}>"
            COMMAND "${VICON_LSL_WINDEPLOYQT}"
                --compiler-runtime
                --no-translations
                "$<TARGET_FILE:vicon-lsl-bridge-gui>"
            COMMAND "${VICON_LSL_POWERSHELL}"
                -NoProfile
                -ExecutionPolicy Bypass
                -File "${bridge_source_dir}/packaging/windows/ensure_msvc_runtime.ps1"
                -DeployDirectory "$<TARGET_FILE_DIR:vicon-lsl-bridge-gui>"
            COMMENT "Deploy Qt and liblsl runtime files next to vicon-lsl-bridge-gui"
            VERBATIM
        )
    endif()

    if(NOT VICON_LSL_POWERSHELL OR NOT VICON_LSL_WINDEPLOYQT)
        message(STATUS
            "pwsh/powershell or windeployqt not found - portable GUI target unavailable")
        return()
    endif()

    get_filename_component(VICON_LSL_QT_BIN_DIR
        "${VICON_LSL_WINDEPLOYQT}" DIRECTORY)
    get_filename_component(VICON_LSL_QT_ROOT
        "${VICON_LSL_QT_BIN_DIR}" DIRECTORY)
    set(VICON_LSL_PACKAGE_QT_LICENSE_ROOT "${VICON_LSL_QT_ROOT}")
    if(VICON_LSL_QT_LICENSE_ROOT)
        if(NOT EXISTS "${VICON_LSL_QT_LICENSE_ROOT}/LICENSES")
            message(FATAL_ERROR
                "VICON_LSL_QT_LICENSE_ROOT must contain a LICENSES directory")
        endif()
        set(VICON_LSL_PACKAGE_QT_LICENSE_ROOT "${VICON_LSL_QT_LICENSE_ROOT}")
    endif()

    set(VICON_LSL_PORTABLE_STAGE
        "${CMAKE_CURRENT_BINARY_DIR}/vicon-lsl-bridge-gui-portable-stage")
    set(VICON_LSL_PORTABLE_PACKAGE_ARGS
        -DeployDir "${VICON_LSL_PORTABLE_STAGE}"
        -OutputExe "${CMAKE_CURRENT_BINARY_DIR}/vicon-lsl-bridge-gui-portable.exe"
        -LauncherExe "$<TARGET_FILE:vicon-lsl-bridge-portable-launcher>"
        -StairModelDir "${bridge_source_dir}/assets/stair_model"
        -ViconSdkDir "${bridge_source_dir}/external/vicon-datastream-sdk"
        -LabRecorderSourceDir "${bridge_source_dir}/../labrecorder"
        -QtRootDir "${VICON_LSL_PACKAGE_QT_LICENSE_ROOT}"
    )
    if(DEFINED liblsl_SOURCE_DIR AND EXISTS "${liblsl_SOURCE_DIR}")
        list(APPEND VICON_LSL_PORTABLE_PACKAGE_ARGS
            -LiblslSourceDir "${liblsl_SOURCE_DIR}")
    endif()
    if(DEFINED VCPKG_INSTALLED_DIR AND DEFINED VCPKG_TARGET_TRIPLET AND
       EXISTS "${VCPKG_INSTALLED_DIR}/${VCPKG_TARGET_TRIPLET}")
        list(APPEND VICON_LSL_PORTABLE_PACKAGE_ARGS
            -BoostRootDir "${VCPKG_INSTALLED_DIR}/${VCPKG_TARGET_TRIPLET}")
    endif()
    if(VICON_LSL_LABRECORDER_DEPLOY_DIR)
        list(APPEND VICON_LSL_PORTABLE_PACKAGE_ARGS
            -LabRecorderDeployDir "${VICON_LSL_LABRECORDER_DEPLOY_DIR}")
    endif()
    if(VICON_LSL_ENIGMA_PROJECT)
        list(APPEND VICON_LSL_PORTABLE_PACKAGE_ARGS
            -Mode Enigma
            -ProjectFile "${VICON_LSL_ENIGMA_PROJECT}")
    endif()
    if(VICON_LSL_ENIGMA_CONSOLE)
        list(APPEND VICON_LSL_PORTABLE_PACKAGE_ARGS
            -EnigmaConsole "${VICON_LSL_ENIGMA_CONSOLE}")
    endif()

    add_custom_target(vicon-lsl-bridge-gui-portable
        COMMAND "${CMAKE_COMMAND}" -E rm -rf "${VICON_LSL_PORTABLE_STAGE}"
        COMMAND "${CMAKE_COMMAND}" -E make_directory "${VICON_LSL_PORTABLE_STAGE}"
        COMMAND "${CMAKE_COMMAND}" -E touch
            "${VICON_LSL_PORTABLE_STAGE}/.vicon-lsl-bridge-package-stage"
        COMMAND "${CMAKE_COMMAND}" -E copy_if_different
            "$<TARGET_FILE:vicon-lsl-bridge-gui>"
            "${VICON_LSL_PORTABLE_STAGE}/vicon-lsl-bridge-gui.exe"
        COMMAND "${CMAKE_COMMAND}" -E copy_if_different
            "$<TARGET_FILE:vicon-lsl-bridge>"
            "${VICON_LSL_PORTABLE_STAGE}/vicon-lsl-bridge.exe"
        COMMAND "${CMAKE_COMMAND}" -E copy_if_different
            "$<TARGET_FILE:${lsl_target}>"
            "${VICON_LSL_PORTABLE_STAGE}/$<TARGET_FILE_NAME:${lsl_target}>"
        COMMAND "${VICON_LSL_WINDEPLOYQT}"
            --compiler-runtime
            --no-translations
            --dir "${VICON_LSL_PORTABLE_STAGE}"
            "${VICON_LSL_PORTABLE_STAGE}/vicon-lsl-bridge-gui.exe"
        COMMAND "${VICON_LSL_POWERSHELL}"
            -NoProfile
            -ExecutionPolicy Bypass
            -File "${bridge_source_dir}/packaging/windows/ensure_msvc_runtime.ps1"
            -DeployDirectory "${VICON_LSL_PORTABLE_STAGE}"
        COMMAND "${VICON_LSL_POWERSHELL}"
            -ExecutionPolicy Bypass
            -File "${bridge_source_dir}/packaging/windows/package_gui_single_exe.ps1"
            ${VICON_LSL_PORTABLE_PACKAGE_ARGS}
        DEPENDS vicon-lsl-bridge vicon-lsl-bridge-gui vicon-lsl-bridge-portable-launcher
        COMMENT "Package vicon-lsl-bridge-gui as a single portable Windows executable"
        VERBATIM
    )
endfunction()
