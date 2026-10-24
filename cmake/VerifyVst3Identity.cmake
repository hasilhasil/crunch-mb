# Permanent Windows VST3 identity for Crunch MB 1.0.4 and later.
# These are JUCE native IDs for ManufacturerCode=Kilo, PluginCode=K7mB.
# Never reuse the former CRMB or VST2-derived identities as aliases.
if(NOT EXISTS "${MANIFEST}")
    message(FATAL_ERROR "Missing VST3 factory manifest: ${MANIFEST}")
endif()
file(READ "${MANIFEST}" manifest_text)
string(REGEX MATCHALL "\"CID\"[ \t\r\n]*:[ \t\r\n]*\"[0-9A-Fa-f]+\"" cid_fields "${manifest_text}")
set(actual_ids "")
foreach(field IN LISTS cid_fields)
    string(REGEX REPLACE ".*\"([0-9A-Fa-f]+)\"$" "\\1" id "${field}")
    string(TOUPPER "${id}" id)
    list(APPEND actual_ids "${id}")
endforeach()
set(expected_ids
    ABCDEF019182FAEB4B696C6F4B376D42
    ABCDEF011234ABCD4B696C6F4B376D42
    ABCDEF01C0DEF00D4B696C6F4B376D42)
if(NOT "${actual_ids}" STREQUAL "${expected_ids}")
    message(FATAL_ERROR "Unexpected VST3 factory identities: ${actual_ids}. Expected: ${expected_ids}")
endif()
if(NOT manifest_text MATCHES "\"Name\"[ \t\r\n]*:[ \t\r\n]*\"Crunch MB\"")
    message(FATAL_ERROR "Unexpected VST3 product name")
endif()
if(NOT manifest_text MATCHES "\"Vendor\"[ \t\r\n]*:[ \t\r\n]*\"Kilo\"")
    message(FATAL_ERROR "Unexpected VST3 manufacturer")
endif()
if(DEFINED EXPECTED_VERSION AND NOT manifest_text MATCHES "\"Version\"[ \t\r\n]*:[ \t\r\n]*\"${EXPECTED_VERSION}\"")
    message(FATAL_ERROR "Unexpected VST3 version")
endif()
foreach(retired_id IN ITEMS
    56535443524D426372756E6368206D62
    56534543524D426372756E6368206D62
    ABCDEF019182FAEB4B696C6F43524D42
    ABCDEF011234ABCD4B696C6F43524D42)
    if(manifest_text MATCHES "${retired_id}")
        message(FATAL_ERROR "Retired ambiguous ID still advertised: ${retired_id}")
    endif()
endforeach()
message(STATUS "Crunch MB fixed native VST3 identity verified: ABCDEF019182FAEB4B696C6F4B376D42")
