add_executable(producer-host-cli-cp
  producer.cpp
)
set_property(TARGET producer-host-cli-cp PROPERTY CXX_STANDARD 20)
target_compile_features(producer-host-cli-cp PRIVATE cxx_std_20)
target_link_libraries(producer-host-cli-cp PRIVATE
  hela-eillance-core
)

add_executable(consumer-host-cli-cp
  consumer.cpp
)
set_property(TARGET consumer-host-cli-cp PROPERTY CXX_STANDARD 20)
target_compile_features(consumer-host-cli-cp PRIVATE cxx_std_20)
target_link_libraries(consumer-host-cli-cp PRIVATE
  hela-eillance-core
)
