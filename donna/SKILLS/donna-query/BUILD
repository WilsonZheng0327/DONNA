load("//learning/gemini/agents/skill_rules:skills.bzl", "agent_skill")
load("//learning/gemini/agents/skills/skill_creator/scripts:skill_defs.bzl", "validate_skill_test")

package(default_visibility = ["//visibility:public"])

agent_skill(
    name = "donna_query",
    srcs = ["SKILL.md"],
)

validate_skill_test(
    name = "validate_donna_query_skill_test",
    strict = True,
)
