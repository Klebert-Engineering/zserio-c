# examples/ — built when ZSERIO_C_BUILD_EXAMPLES=ON.
#
# Each example also supports a standalone build from its own
# directory (the integrator-facing path documented in its README);
# this wrapper builds them against the parent build's runtime
# target so CI can keep the examples compiling.

add_subdirectory(embedded_decode)
