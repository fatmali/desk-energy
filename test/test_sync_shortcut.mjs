import assert from "node:assert/strict";
import { readFileSync } from "node:fs";
import { buildHealthData } from "../tools/shortcuts/build-sync.mjs";

const workflow = buildHealthData();
const previous = JSON.parse(readFileSync(new URL("../tools/shortcuts/health-data.json", import.meta.url), "utf8"));
const withoutInstructions = value => ({ ...value, WFWorkflowActions: value.WFWorkflowActions.slice(1) });
assert.deepEqual(withoutInstructions(workflow), withoutInstructions(previous),
  "Cleanup must preserve the working Shortcut's filters, metadata and data wiring");
const seen = new Set();
const allowed = new Set(["comment", "nothing", "filter.health.quantity", "properties.health.quantity", "format.date", "gettext"]);
for (const action of workflow.WFWorkflowActions) {
  assert(allowed.has(action.WFWorkflowActionIdentifier.slice("is.workflow.actions.".length)));
  const parameters = action.WFWorkflowActionParameters;
  assert(!seen.has(parameters.UUID));
  seen.add(parameters.UUID);
  function check(value) {
    if (!value || typeof value !== "object") return;
    if (value.OutputUUID) assert(seen.has(value.OutputUUID));
    if (value.attachmentsByRange) {
      for (const [range] of Object.entries(value.attachmentsByRange)) {
        const index = Number(range.match(/^\{(\d+), 1\}$/)?.[1]);
        assert.equal(value.string[index], "\uFFFC");
      }
    }
    for (const child of Object.values(value)) check(child);
  }
  check(parameters);
}
const queries = workflow.WFWorkflowActions.filter(action => action.WFWorkflowActionIdentifier.endsWith(".filter.health.quantity"));
assert.equal(queries.length, 3);
for (const [index, query] of queries.entries()) {
  const p = query.WFWorkflowActionParameters;
  assert.equal(p.WFContentItemLimitEnabled, false);
  assert.equal(p.WFContentItemFilter.Value.WFActionParameterFilterTemplates.length, 2);
  const [type, date] = p.WFContentItemFilter.Value.WFActionParameterFilterTemplates;
  assert.equal(type.Property, "Type");
  assert.equal(type.Values.Enumeration.Value, ["Heart Rate Variability", "Resting Heart Rate", "Active Calories"][index]);
  assert.equal(type.Removable, false);
  assert.equal(date.Property, "Start Date");
  assert.equal(date.Removable, false);
  assert.equal(date.Operator, index === 2 ? 1002 : 1001);
  if (index !== 2) assert.deepEqual(date.Values, { Number: "3650", Unit: 16 });
}
const formats = workflow.WFWorkflowActions.filter(action => action.WFWorkflowActionIdentifier.endsWith(".format.date"));
assert.equal(formats.length, 4);
assert(formats.every(action => action.WFWorkflowActionParameters.WFISO8601IncludeTime === true));
const final = workflow.WFWorkflowActions.at(-1).WFWorkflowActionParameters.WFTextActionText.Value;
assert(final.string.startsWith("DESK_ENERGY_V1\n"));
assert.equal(Object.keys(final.attachmentsByRange).length, 8);
assert(!JSON.stringify(workflow).includes("TOKEN"));
console.log("PASS: 20-action data-only Shortcut, original Health filters, eight aligned value/date/source outputs, ISO timestamps, no network/credentials/calculation");
