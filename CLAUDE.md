# Working with the user on this project

## Explain new terms from the ground up

When a new term, function, library, header, or concept comes up (e.g. `errno`,
`strerror`, `<cerrno>`, `inet_pton`'s return codes), don't just use it or gloss
it with a one-liner — explain it from the bottom, assuming no prior
familiarity:

- Say what problem it exists to solve before naming it.
- Use a plain-language analogy where it helps.
- Show it live/concretely if possible (a small runnable snippet or command)
  rather than only describing it in the abstract.
- Only after that, tie it back to the actual line of code in the project that
  prompted the explanation.

Do this *before* proposing or making a code change that uses the new thing —
walk through what it is first, then show how it would be used in isolation,
then apply it to the real file. Don't skip straight to editing.

If an explanation doesn't land, don't just rephrase more tersely — slow down
further and re-explain from a more basic starting point (see the `errno` /
`strerror` example in this project's history: first pass was too abstract,
second pass used a concrete `os.strerror(code)` demo and it clicked).
