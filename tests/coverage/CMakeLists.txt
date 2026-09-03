add_executable(avs_coverage_inventory coverage_inventory.cpp)
target_link_libraries(avs_coverage_inventory PRIVATE
  "$<LINK_LIBRARY:WHOLE_ARCHIVE,AvsCoreExternal>"
)
