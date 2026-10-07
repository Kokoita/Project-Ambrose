<!-- Project Ambrose by Imjustchico: The loginserver maintenance banner and audited operator control, with a player-facing reason, an optional published UTC window, and the last account and time that changed maintenance. -->
<script lang="ts">
    import * as Card from "$lib/components/ui/card/index.js";
    import { Button } from "$lib/components/ui/button/index.js";
    import { Input } from "$lib/components/ui/input/index.js";
    import { Label } from "$lib/components/ui/label/index.js";
    import { Textarea } from "$lib/components/ui/textarea/index.js";
    import { ApiError } from "$lib/api.svelte.js";
    import { changeSettings, settingHistory } from "$lib/supervision.svelte.js";
    import { isLocked, textBytes, type Setting } from "$lib/settings.js";
    import type { SettingHistoryAnswer } from "$lib/schemas.js";
    import { Switch } from "$lib/components/ui/switch/index.js";
    import StatusBadge from "./StatusBadge.svelte";

    type Props = {
        app: string;
        settings: Setting[];
        canEdit: boolean;
        done: (message: string) => void;
    };

    let { app, settings, canEdit, done }: Props = $props();
    let enabled = $state(false);
    let reason = $state("");
    let windowStart = $state("");
    let windowEnd = $state("");
    let busy = $state(false);
    let failure = $state("");
    let historyFailure = $state("");
    let lastChange = $state<SettingHistoryAnswer["entries"][number] | null>(null);
    let historyFor = "";

    const maintenance = $derived(settings.find((setting) => setting.key === "Login.Maintenance"));
    const maintenanceReason = $derived(settings.find((setting) => setting.key === "Login.MaintenanceReason"));
    const startSetting = $derived(settings.find((setting) => setting.key === "Login.MaintenanceWindowStart"));
    const endSetting = $derived(settings.find((setting) => setting.key === "Login.MaintenanceWindowEnd"));
    const active = $derived(maintenance?.value === "true");
    const locked = $derived([maintenance, maintenanceReason, startSetting, endSetting].some((setting) => !setting || isLocked(setting)));

    function toLocalInput(value: string | undefined): string {
        const epoch = Number(value ?? "0");
        if (!Number.isFinite(epoch) || epoch <= 0) return "";
        const date = new Date(epoch * 1000);
        return new Date(date.getTime() - date.getTimezoneOffset() * 60_000).toISOString().slice(0, 16);
    }

    function toEpoch(value: string): number | null {
        if (value === "") return 0;
        const timestamp = new Date(value).getTime();
        return Number.isFinite(timestamp) ? Math.floor(timestamp / 1000) : null;
    }

    $effect(() => {
        if (!maintenance || !maintenanceReason || !startSetting || !endSetting) return;
        enabled = maintenance.value === "true";
        reason = maintenanceReason.value;
        windowStart = toLocalInput(startSetting.value);
        windowEnd = toLocalInput(endSetting.value);
    });

    async function loadHistory(name: string) {
        historyFailure = "";
        try {
            const answer = await settingHistory(name, "Login.Maintenance");
            lastChange = answer.entries[0] ?? null;
        } catch (problem) {
            historyFailure = problem instanceof ApiError ? problem.message : "The maintenance audit history could not be read";
        }
    }

    $effect(() => {
        const name = app;
        if (name === "" || historyFor === name) return;
        historyFor = name;
        void loadHistory(name);
    });

    const startEpoch = $derived(toEpoch(windowStart));
    const endEpoch = $derived(toEpoch(windowEnd));
    const windowProblem = $derived(
        startEpoch === null || endEpoch === null
            ? "The maintenance window must contain valid local dates"
            : (startEpoch === 0) !== (endEpoch === 0) || (startEpoch > 0 && endEpoch <= startEpoch)
              ? "Give both window dates, with the end after the start"
              : null,
    );
    const reasonProblem = $derived(
        reason.trim() === ""
            ? "Give the maintenance reason; it is shown to players and saved in the audit history"
            : textBytes(reason.trim()) > 255
              ? "The maintenance reason must be at most 255 bytes"
              : null,
    );

    function localTime(epochSeconds: number): string {
        return new Intl.DateTimeFormat(undefined, { dateStyle: "medium", timeStyle: "short" }).format(new Date(epochSeconds * 1000));
    }

    async function save() {
        if (!maintenance || !maintenanceReason || !startSetting || !endSetting || locked || !canEdit || busy || reasonProblem || windowProblem) return;
        busy = true;
        failure = "";
        try {
            const answer = await changeSettings(
                app,
                [
                    { key: "Login.Maintenance", value: enabled },
                    { key: "Login.MaintenanceReason", value: reason.trim() },
                    { key: "Login.MaintenanceWindowStart", value: startEpoch ?? 0 },
                    { key: "Login.MaintenanceWindowEnd", value: endEpoch ?? 0 },
                ],
                reason.trim(),
            );
            await loadHistory(app);
            done(answer.message);
        } catch (problem) {
            failure = problem instanceof ApiError ? problem.message : "The maintenance setting change failed";
        } finally {
            busy = false;
        }
    }
</script>

<Card.Root class={active ? "border-warning/50 bg-warning/5 shadow-xs" : "shadow-xs"}>
    <Card.Header>
        <div class="flex flex-wrap items-center gap-2">
            <Card.Title>Installation maintenance</Card.Title>
            <StatusBadge tone={active ? "waiting" : "healthy"}>{active ? "Active" : "Off"}</StatusBadge>
        </div>
        <Card.Description>
            {#if active}
                Players cannot sign in; accounts at the configured bypass level can. Already-connected game sessions are not disconnected.
            {:else}
                Control player sign-ins and publish an optional maintenance window.
            {/if}
        </Card.Description>
    </Card.Header>
    <Card.Content class="space-y-5">
        {#if active}
            <p class="text-sm" role="status">{reason || maintenanceReason?.value}</p>
            {#if lastChange}
                <p class="text-xs text-muted-foreground">
                    Changed by {lastChange.who} at {localTime(lastChange.epoch_seconds)} · {lastChange.reason}
                </p>
            {/if}
        {/if}
        {#if Number(startSetting?.value ?? "0") > 0 && Number(endSetting?.value ?? "0") > 0}
            <p class="text-xs text-muted-foreground">
                Window: {localTime(Number(startSetting?.value))}–{localTime(Number(endSetting?.value))} (your local time)
            </p>
        {/if}
        {#if historyFailure}
            <p class="text-xs text-destructive" role="alert">{historyFailure}</p>
        {/if}
        {#if failure}
            <p class="rounded-md border border-destructive/30 bg-destructive/5 p-3 text-sm text-destructive" role="alert">{failure}</p>
        {/if}
        <div class="flex items-center gap-3">
            <Switch id="login-maintenance" bind:checked={enabled} disabled={!canEdit || locked || busy} />
            <Label for="login-maintenance">{enabled ? "Refuse player sign-ins" : "Accept player sign-ins"}</Label>
        </div>
        <div class="space-y-2">
            <Label for="maintenance-reason">Reason shown to players and recorded for this change</Label>
            <Textarea id="maintenance-reason" bind:value={reason} rows={2} maxlength="255" disabled={!canEdit || locked || busy} />
            {#if reasonProblem}<p class="text-xs text-destructive" role="alert">{reasonProblem}</p>{/if}
        </div>
        <div class="grid gap-4 md:grid-cols-2">
            <div class="space-y-2">
                <Label for="maintenance-window-start">Window starts (optional, local time)</Label>
                <Input id="maintenance-window-start" type="datetime-local" bind:value={windowStart} disabled={!canEdit || locked || busy} />
            </div>
            <div class="space-y-2">
                <Label for="maintenance-window-end">Window ends (optional, local time)</Label>
                <Input id="maintenance-window-end" type="datetime-local" bind:value={windowEnd} disabled={!canEdit || locked || busy} />
            </div>
        </div>
        {#if windowProblem}<p class="text-xs text-destructive" role="alert">{windowProblem}</p>{/if}
        <div class="flex items-center justify-between gap-3">
            {#if locked}<StatusBadge tone="unknown">One or more settings are locked by a higher layer</StatusBadge>{/if}
            <Button onclick={() => void save()} disabled={!canEdit || locked || busy || reasonProblem !== null || windowProblem !== null}>
                {busy ? "Saving…" : "Save maintenance changes"}
            </Button>
        </div>
    </Card.Content>
</Card.Root>
