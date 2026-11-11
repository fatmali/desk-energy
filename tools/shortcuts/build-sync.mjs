import { writeFileSync } from "node:fs";
import { execFileSync } from "node:child_process";
import { fileURLToPath } from "node:url";
import { dirname, join } from "node:path";

const directory = dirname(fileURLToPath(import.meta.url));

export function buildHealthData() {
  const actions = [];
  let sequence = 0;
  function add(name, parameters, outputName) {
    const uuid = `D35CE003-0000-4000-8000-${String(++sequence).padStart(12, "0")}`;
    actions.push({
      WFWorkflowActionIdentifier: `is.workflow.actions.${name}`,
      WFWorkflowActionParameters: { ...parameters, UUID: uuid },
    });
    return { WFSerializationType: "WFTextTokenAttachment",
      Value: { Type: "ActionOutput", OutputUUID: uuid, OutputName: outputName } };
  }
  function text(string, attachmentsByRange = {}) {
    return { WFSerializationType: "WFTextTokenString", Value: { string, attachmentsByRange } };
  }
  const outputs = {};
  add("comment", { WFCommentActionText:
    "Health data reader for the Desk Energy Scriptable script. No calculations, clipboard access, file writes, or network calls. Three Health queries read HRV, resting heart rate and today's active calories. Original uncapped heart-metric history is retained. The final Text output is private Health data: do not share or log it. Add Scriptable's Run Script action after Text, select Desk Energy, set Parameter to Text, and leave Run in App OFF. The script uploads only a small summary to your authenticated Cloudflare relay, not these histories. Run once unlocked and verify permissions; test locked-phone automation separately."
  }, "Comment");
  const groups = [
    ["Heart Rate Variability", [["hrv_values", "Value"], ["hrv_dates", "Start Date"]]],
    ["Resting Heart Rate", [["rhr_values", "Value"], ["rhr_dates", "Start Date"]]],
    ["Active Calories", [["activity_values", "Value"], ["activity_starts", "Start Date"],
      ["activity_ends", "End Date"], ["activity_sources", "Source"]]],
  ];
  for (const [type, properties] of groups) {
    add("nothing", {}, "Nothing");
    const parameters = {
      WFContentItemLimitEnabled: false,
      WFContentItemLimitNumber: 2,
      WFContentItemFilter: {
        WFSerializationType: "WFContentPredicateTableTemplate",
        Value: {
          WFActionParameterFilterPrefix: 1,
          WFContentPredicateBoundedDate: false,
          WFActionParameterFilterTemplates: [
            {
              Bounded: true, Operator: 4, Property: "Type", Removable: false,
              Values: { Enumeration: { WFSerializationType: "WFStringSubstitutableState", Value: type } },
            },
            {
              Bounded: true, Operator: type === "Active Calories" ? 1002 : 1001,
              Property: "Start Date", Removable: false,
              Values: type === "Active Calories" ? {} : { Number: "3650", Unit: 16 },
            },
          ],
        },
      },
    };
    const samples = add("filter.health.quantity", parameters, "Health Samples");
    for (const [field, property] of properties) {
      let value = add("properties.health.quantity", {
        WFInput: samples, WFContentItemPropertyName: property,
      }, property);
      if (property.endsWith("Date")) {
        // Implicit input is the preceding Get Details result.
        value = add("format.date", { WFDateFormatStyle: "ISO 8601", WFISO8601IncludeTime: true }, "Formatted Date");
      }
      outputs[field] = value;
    }
  }
  let payload = "DESK_ENERGY_V1";
  const attachments = {};
  for (const [field, output] of Object.entries(outputs)) {
    payload += `\n[${field}]\n`;
    attachments[`{${payload.length}, 1}`] = output.Value;
    payload += "\uFFFC";
  }
  add("gettext", { WFTextActionText: text(payload, attachments) }, "Text");
  return {
    WFWorkflowName: "Desk Energy Health Data",
    WFWorkflowClientVersion: "5114.0.6.204",
    WFWorkflowMinimumClientVersion: 900,
    WFWorkflowMinimumClientVersionString: "900",
    WFWorkflowIcon: { WFWorkflowIconStartColor: 3031607807, WFWorkflowIconGlyphNumber: 59511 },
    WFWorkflowTypes: [],
    WFWorkflowInputContentItemClasses: [],
    WFWorkflowHasOutputFallback: false,
    WFWorkflowActions: actions, WFWorkflowImportQuestions: [],
  };
}

if (process.argv[1] === fileURLToPath(import.meta.url)) {
  const workflow = buildHealthData();
  const path = join(directory, "health-data.json");
  writeFileSync(path, `${JSON.stringify(workflow, null, 2)}\n`);
  execFileSync("plutil", ["-convert", "binary1", "-o", join(directory, "health-data.unsigned.shortcut"), path]);
  console.log(`Built ${workflow.WFWorkflowActions.length} data-only actions. Add Scriptable's Run Script action on the iPhone.`);
}
