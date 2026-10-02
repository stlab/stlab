target_sources( stlab INTERFACE
  FILE_SET headers
  TYPE HEADERS
  BASE_DIRS ../
  FILES
    algorithm/reverse.hpp

    concurrency/await.hpp
    concurrency/channel.hpp
    concurrency/concurrency.hpp
    concurrency/future.hpp
    concurrency/progress.hpp
    concurrency/ready_future.hpp
    concurrency/serial_queue.hpp
    concurrency/traits.hpp
    concurrency/tuple_algorithm.hpp
    concurrency/utility.hpp

    iterator/set_next.hpp
    iterator/concepts.hpp

    test/model.hpp

    forest.hpp
    forest_algorithms.hpp
    functional.hpp
    memory.hpp
    scope.hpp
    utility.hpp
    version.hpp
)

configure_file(config.hpp.in "${PROJECT_BINARY_DIR}/include/stlab/config.hpp" @ONLY)

target_sources( stlab INTERFACE
  FILE_SET headers
  TYPE HEADERS
  BASE_DIRS ${PROJECT_BINARY_DIR}/include
  FILES
    ${PROJECT_BINARY_DIR}/include/stlab/config.hpp
)
