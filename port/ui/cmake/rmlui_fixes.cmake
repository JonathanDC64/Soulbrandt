# Fixes to the pinned RmlUi, made to its sources at configure time, after FetchContent_MakeAvailable(RmlUi).
#
# How a fix is made:
# - Each fix replaces one line's text, so the file's line endings never matter.
# - A fix is skipped when its new text is already there. CI's cached copy of the sources (tools/ci/fetch_src.py) and a
#   FETCHCONTENT_SOURCE_DIR_RMLUI tree get the same fix as a fresh clone.
# - When the old text is missing, or appears more than once, the configure stops. That happens after the RmlUi pin
#   moves: check whether RmlUi has fixed it, then update the fix or drop it.
#
# CI keys its cache of the sources on this file too (ci.yml, host.yml), so a changed fix never meets an older one.

function(des_rmlui_fix file old new)
  set(path "${rmlui_SOURCE_DIR}/${file}")
  file(READ "${path}" text)
  string(FIND "${text}" "${new}" done)
  if(NOT done EQUAL -1)
    return()
  endif()
  string(FIND "${text}" "${old}" first)
  string(FIND "${text}" "${old}" last REVERSE)
  if(first EQUAL -1 OR NOT first EQUAL last)
    message(FATAL_ERROR "RmlUi fix: ${file} does not have the text the fix replaces exactly once "
                        "(port/ui/cmake/rmlui_fixes.cmake). Did the RmlUi pin move?")
  endif()
  string(REPLACE "${old}" "${new}" text "${text}")
  file(WRITE "${path}" "${text}")
  message(STATUS "RmlUi fix made: ${file}")
endfunction()

# 1. Transitions and animations on `transform` no longer change the element's own style.
#
#    The bug:
#    - An animation key copies the element's transform property. A Variant copies only the pointer, so the key
#      shares the Transform with the element's style and with the style sheet.
#    - PrepareTransforms then changes that Transform in place: dp and % become px, at that moment's dp ratio and box
#      size.
#    - So after a UI scale change (a new dp ratio), the element keeps the old ratio's offset. Setting the document's
#      sheets again (host.cpp's restyle_documents) does not help: the new sheets share the same Transform.
#
#    Seen as: the F1 menu's bar (menu.rcss, translateY(-65dp) when closed) still partly in view under Settings after a
#    larger UI scale, by what the old ratio was short of the new one.
#
#    The fix gives each key a Transform of its own. RmlUi's master still had the bug on 2026-11-18.
#    ui_style_check checks it.
des_rmlui_fix(Source/Core/ElementAnimation.cpp
  "bool must_decompose = false;"
  "/* port fix 1, port/ui/cmake/rmlui_fixes.cmake: the key's own Transform */ property.value = MakeShared<Transform>(*property.value.GetReference<TransformPtr>()); bool must_decompose = false;")
