---
description: Produce a concise Cardputer ADV device summary for the active Claude response
---

When the user asks for a Cardputer summary, produce a normal answer plus one
tiny device summary suitable for a 240x135 display.

Use this exact wrapper:

```text
<device_summary>
One concise status-style summary. Max 180 characters. Prefer the answer,
decision, or next action. No markdown table. No code block.
</device_summary>
```

Do not put private secrets, full transcripts, or long command output in the
device summary.

