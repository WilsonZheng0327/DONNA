load("//tools/build_defs/python:py_binary.bzl", "py_binary")
load("//tools/build_defs/python:py_library.bzl", "py_library")

package(default_visibility = ["//visibility:public"])

py_library(
    name = "donna_cli_lib",
    srcs = ["donna_cli.py"],
)

py_binary(
    name = "donna_cli",
    srcs = ["donna_cli.py"],
    deps = [":donna_cli_lib"],
)

py_binary(
    name = "donna_sync",
    srcs = ["donna_sync.py"],
    deps = [
        ":donna_cli_lib",
        "//third_party/py/google/auth",
        "//third_party/py/googleapiclient",
    ],
)
