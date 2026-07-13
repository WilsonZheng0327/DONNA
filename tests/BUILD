load("//tools/build_defs/python:py_test.bzl", "py_test")

package(default_visibility = ["//visibility:public"])

py_test(
    name = "donna_cli_test",
    srcs = ["donna_cli_test.py"],
    deps = ["//experimental/users/wilsonzheng/DONNA/src/agent:donna_cli"],
)

py_test(
    name = "donna_agent_test",
    srcs = ["donna_agent_test.py"],
    deps = ["//experimental/users/wilsonzheng/DONNA/src/agent:donna_cli"],
)
