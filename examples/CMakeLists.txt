add_executable(memrogue_example simple_app.c)
add_dependencies(memrogue_example memrogue_intercept)

add_test(
	NAME MemRogueInterceptionExample
	COMMAND ${CMAKE_COMMAND} -E env LD_PRELOAD=$<TARGET_FILE:memrogue_intercept> $<TARGET_FILE:memrogue_example>
)
