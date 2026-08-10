# Device Summary Prompt Template

Use this with the future ADV prompt-input path. The firmware already recognizes
`<device_summary>...</device_summary>` inside assistant turn events and displays
that text instead of a larger excerpt.

Append this to prompts sent from the Cardputer ADV:

```text
For the M5Stack Cardputer ADV display, include one tiny device summary before
or after the main answer:

<device_summary>
One concise status-style summary for a 240x135 display. Max 180 characters.
Prefer the answer, decision, or next action. No markdown table. No code block.
</device_summary>
```

The normal Claude answer can remain unchanged outside the tag. If Claude omits
the tag, the firmware falls back to a fixed-size excerpt of the assistant turn.
