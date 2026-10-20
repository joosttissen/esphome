# Feature: Generic PIN-Protected Cover Operations in ESPHome

## Your role and execution strategy

You are the **lead software architect and coordinating agent** for a substantial open-source contribution to `esphome/esphome`.

Implement a generic, device-enforced PIN authorization mechanism for ESPHome covers, suitable for submission to the upstream project.

**MANDATORY: Use multiple subagents throughout this development task.**

This feature involves C++, Python, protobuf, security, integration testing and documentation. Do not implement everything within one agent context.

Use the available native subagent orchestration capabilities:

- GitHub Copilot CLI: use `/fleet` or equivalent subagent delegation.
- VS Code Copilot: use available subagent tools and parallel delegation.
- Other supported environments: use their native delegation mechanism.

The main agent must coordinate architecture, dependencies, integration and final acceptance.

If true subagent execution is unavailable, explicitly report that limitation rather than pretending delegation occurred.

Use parallel execution wherever tasks are independent. Serialize work when dependencies require it.

Do not stop after planning. Implement, test, integrate and document the feature.

---

## 1. Background

ESPHome provides a generic `cover` abstraction for garage doors, gates, shutters, blinds and other motorized covers.

Currently, clients with access to the ESPHome native API can command covers without operation-specific authorization.

Some operations, particularly opening garage doors or gates, should require an additional credential.

The initial use case is:

- Hardware: M5Atom (ESP32)
- Firmware: ESPHome
- Integration: native `hoermann_hcp` cover platform
- Motor: Hörmann garage door controller
- Frontend: Home Assistant

However, **the implementation must not be specific to Hörmann, ESP32 or Home Assistant**.

It should support any ESPHome cover platform and remain completely functional without Home Assistant connectivity.

Authentication must happen on the ESPHome device itself.

A PIN dialog implemented only in Home Assistant is insufficient because it can be bypassed through direct device commands.

The longer-term goal is compatibility with role-based authorization and step-up authentication in Home Assistant. Implementing Home Assistant RBAC is outside this project's scope.

---

## 2. Primary requirements

Implement optional PIN authorization at the generic ESPHome cover level.

The implementation must provide:

1. Per-cover configuration of protected operations.
2. Local device-side PIN validation.
3. Authorization enforcement before invoking the cover's physical control logic.
4. Protection against alternative command paths.
5. Brute-force protection and rate limiting.
6. Native API credential transmission.
7. Native API capability discovery.
8. Compatibility with existing clients and unprotected covers.
9. Automated unit, configuration and protocol tests.
10. End-to-end testing using a real virtual ESPHome device.
11. Documentation suitable for upstream contribution.
12. Independent adversarial security review by a subagent.

Do not use a solution based on Home Assistant automations, custom dashboard cards or custom YAML lambdas.

Implement this as a maintainable ESPHome Core feature.

---

## 3. Proposed configuration

Design a configuration interface resembling:

```yaml
api:
  encryption:
    key: !secret api_encryption_key

cover:
  - platform: hoermann_hcp
    id: garage_door
    name: "Garage Door"
    device_class: garage

    authorization:
      opening:
        pin: !secret garage_pin
        max_attempts: 5
        lockout: 5min
```

This is proposed YAML, not an existing ESPHome feature.

Adjust naming if necessary to conform to project conventions.

### Configuration behavior

- `authorization` is optional.
- Unprotected covers continue operating as before.
- `opening` protects every remote operation capable of opening the cover.
- `pin` specifies the credential validated by the ESPHome device.
- `max_attempts` and `lockout` provide configurable brute-force protection.
- PINs can be supplied using `secrets.yaml`.
- Invalid configurations must produce meaningful validation errors.
- Insecure transport configurations must be rejected when appropriate.
- Multiple covers must support independent authorization policies.

For the first implementation, a single PIN per cover is sufficient.

Design the underlying abstractions to permit additional protected operations later without introducing an unnecessarily complex authentication framework today.

---

## 4. Authorization behavior

### Protected operations

With opening authorization enabled, require a valid PIN for:

- `open`
- Setting position to 100%
- Partial opening
- Any target position greater than fully closed
- `toggle` or impulse operations that could cause opening
- Tilt operations capable of creating an opening
- Commands whose physical effect cannot be reliably determined

For v1, conservatively protecting all nonzero position requests is acceptable.

Never rely exclusively on the cover's reported current position to decide whether a command is safe.

Malformed or conflicting commands must fail closed.

### Unprotected operations

By default, allow:

- Close
- Stop
- Reading the cover's position and state

Closing and stopping must continue working during authentication lockout.

### Trusted local commands

Investigate how ESPHome currently handles operations issued by:

- Native API clients
- MQTT
- Web server
- Local automations
- Physical button inputs
- ESPHome scripts
- Custom API actions
- Device-specific commands

Define a clear separation between trusted firmware-originated commands and externally initiated commands.

A physical wall switch or internal safety automation may need to operate without a PIN.

However, remotely callable actions must not accidentally acquire trusted-local privileges.

Do not implement a generic remotely accessible bypass.

Document any cases in which user-authored automations can deliberately expose a privileged operation.

---

## 5. Generic ESPHome architecture

Before implementation, inspect:

- `esphome/components/cover/`
- `Cover`
- `CoverCall`
- `CoverCall::perform()`
- `CoverCall::validate_()`
- `CoverTraits`
- `esphome/components/api/api.proto`
- `APIConnection::on_cover_command_request()`
- API discovery and entity metadata
- Cover automation actions
- MQTT cover command handling
- Web-server cover command handling
- Existing authentication and security utilities

Prefer a reusable authorization policy associated with the generic `Cover` abstraction.

Enforce authorization before execution reaches a platform's `control()` implementation.

Evaluate whether the existing `CoverCall` mechanism can serve as the enforcement boundary.

Be particularly careful about command provenance: an API request must not be able to impersonate a trusted internal call.

### Architecture constraints

- No duplicated PIN validation in individual cover platforms.
- No unnecessary dynamic allocations.
- No blocking operation in the main execution loop.
- Negligible overhead for covers without authorization.
- No dependency on Home Assistant.
- No cloud or external authentication service.
- Support resource-constrained ESP32 devices.
- Preserve existing public APIs wherever possible.

Keep implementation components cohesive and independently testable.

---

## 6. Native API protocol

Extend the native ESPHome API to support authenticated cover commands.

Conceptually:

```protobuf
message CoverCommandRequest {
  // Existing fields remain unchanged.

  string code = ...;
}
```

The actual field number must be selected from currently unused numbers.

Extend cover discovery metadata:

```protobuf
message ListEntitiesCoverResponse {
  // Existing fields remain unchanged.

  bool requires_code_to_open = ...;
}
```

### Requirements

- Maintain protobuf backward compatibility.
- Do not change existing field numbers.
- Credentials must never appear in discovery metadata.
- Unprotected operations must continue supporting existing requests.
- Protected operations must reject missing credentials.
- Old clients must not bypass the requirement.
- Apply reasonable credential-length limits.
- Regenerate generated protocol files using official tooling.
- Follow existing ESPHome API conventions.

Investigate the corresponding changes required in `aioesphomeapi`.

If that repository is available, implement the client changes separately. Otherwise, provide a tested integration plan or patch specification.

Do not assume Home Assistant Core already understands these fields.

### Operation feedback

Determine whether the current API can communicate that an operation was denied.

Consider:

- Authentication required
- Invalid credential
- Temporarily locked
- Operation rejected

Avoid introducing a large new response protocol unless necessary.

If operation-result reporting must be postponed, explicitly document the compatibility and user-feedback implications.

---

## 7. Security requirements

Authentication must be enforced locally by ESPHome.

### Credential handling

- Never publish configured PINs in entity states.
- Never expose PINs in entity discovery.
- Never log submitted credentials.
- Avoid keeping submitted credentials in memory unnecessarily.
- Validate PIN length and format.
- Prefer constant-time comparison where practical.
- Reuse existing project security utilities where appropriate.
- Protect credential transmission using encrypted native API communication.

Use ESPHome's existing Noise-based native API encryption.

Do not introduce plaintext PIN transport through MQTT, HTTP URLs or web-server query strings.

If a command source cannot securely provide credentials, its protected operations must fail closed.

### Brute-force protection

Provide per-cover rate limiting.

Default example:

```yaml
max_attempts: 5
lockout: 5min
```

Requirements:

- Track invalid credential attempts.
- Reject protected operations during lockout.
- Keep close/stop available.
- Use nonblocking timers.
- Handle `millis()` rollover correctly.
- Define behavior when the device restarts.
- Avoid trivial reboot-based brute-force resets.
- Avoid excessive flash writes.
- Prevent missing credentials from older clients causing an easy denial-of-service lockout.

Investigate ESPHome preferences and persistent state support.

If a fully reboot-safe lockout cannot be implemented reasonably, document the security limitation rather than claiming complete brute-force protection.

### Threat model

The protected boundary is remote command execution by clients that can otherwise reach the ESPHome device.

This does not provide protection against:

- Physical firmware extraction
- An attacker who can reflash the controller
- A compromised trusted firmware automation
- Independent radio remotes communicating directly with the garage motor

Document these limits accurately.

---

## 8. Hörmann integration validation

Use `hoermann_hcp` as a real integration compatibility target.

Do not place the generic authorization logic inside the Hörmann driver.

Investigate:

- Opening
- Closing
- Stopping
- Position control
- Impulse/toggle operation
- Vent
- Half-open

The Hörmann integration can expose additional commands outside the standard cover interface.

These must not provide an unauthenticated remote opening path when protection is enabled.

Prefer shared authorization handling or secure configuration validation over duplicating credential checks.

Preserve the existing Hörmann communication protocol and motor safety behavior.

Since virtual testing cannot reproduce the actual HCP hardware, separate platform-specific static/unit validation from tests that genuinely require physical hardware.

---

## 9. Mandatory virtual ESPHome device testing

**This is a required deliverable, not an optional enhancement.**

Implement an end-to-end integration test using an actual ESPHome firmware instance running on a development computer.

Do not rely exclusively on mocked C++ classes or Python unit tests.

Use ESPHome's existing `host` platform, which supports running firmware locally on Linux and compatible development environments.

Use a `template` cover to simulate a garage door.

### 9.1 Virtual device

Create a test configuration, for example:

`tests/integration/fixtures/protected_cover_host.yaml`

Use the repository's existing test-directory conventions if different.

Conceptual example:

```yaml
esphome:
  name: virtual-protected-garage

host:

logger:

api:
  encryption:
    key: !secret api_encryption_key

cover:
  - platform: template
    id: virtual_garage
    name: "Virtual Garage"
    device_class: garage
    has_position: true
    optimistic: true

    authorization:
      opening:
        pin: !secret test_garage_pin
        max_attempts: 5
        lockout: 5s
```

Adjust the final configuration to whatever is supported by the implemented feature and current ESPHome schema.

Use deterministic test secrets, isolated to the test environment.

Do not depend on actual hardware or external services.

### 9.2 Instrumentation

The virtual cover must expose observable evidence that a command reached the simulated actuator.

Instrument the virtual actuator using state, counters or another deterministic mechanism.

For example:

- Number of accepted open commands
- Number of accepted close commands
- Current simulated position
- Last executed operation

The test must be able to distinguish a rejected command from an accepted command that produces no visible movement.

Unauthorized commands must not increment actuator execution counters or trigger physical-control actions.

### 9.3 Real native API integration

Compile and launch the virtual ESPHome firmware as a subprocess.

Connect to it over the real native API.

Use `aioesphomeapi` or a compatible test client capable of sending the new credential field.

Where necessary, extend the client in the test environment so it can exercise the new protocol.

Tests must use real serialized API messages over the actual encrypted transport.

A mocked API server is insufficient.

### 9.4 Required end-to-end test cases

| Test | Expected result |
|---|---|
| Open with correct PIN | Accepted |
| Open with incorrect PIN | Rejected |
| Open without PIN | Rejected |
| Close without PIN | Accepted |
| Stop without PIN | Accepted |
| Partial open with PIN | Accepted |
| Partial open without PIN | Rejected |
| Toggle without PIN | Rejected |
| Invalid PIN repeatedly | Lockout |
| Correct PIN during lockout | Rejected |
| Correct PIN after lockout | Accepted |
| Old API client without PIN field | Cannot open |
| Unprotected virtual cover | Normal behavior |

Also test malformed credentials, protocol compatibility and cover-state behavior.

### 9.5 Alternative command paths

Where supported in the test environment, attempt the same operation through:

- Native API
- Legacy native API message format
- ESPHome web server
- MQTT
- Exposed API actions
- Alternative cover operations

Any supported unauthenticated path capable of opening the protected cover is a security failure.

Do not claim an interface was tested if it was not actually exercised.

### 9.6 Test automation

Provide a reproducible automated test runner.

The test should:

1. Validate the ESPHome YAML.
2. Compile the host firmware.
3. Start the virtual device.
4. Wait for native API readiness.
5. Connect with encryption.
6. Discover the virtual cover.
7. Execute positive and negative authorization tests.
8. Verify actual simulated actuator behavior.
9. Test rate limiting.
10. Terminate the firmware process.
11. Return a nonzero exit code on failure.

Use dynamic ports or isolated execution environments where possible to avoid conflicts between simultaneous test runs.

Ensure subprocesses are cleaned up even when tests fail.

Keep tests deterministic and suitable for CI.

### 9.7 Additional virtual platforms

If practical, instantiate both:

- A protected template cover
- An unprotected template cover

This verifies that the feature is optional and does not change ordinary behavior.

If another virtual cover platform provides better position/tilt simulation, consider including it.

The goal is to test the generic cover implementation, not the Hörmann motor emulator.

### 9.8 Test evidence

At completion, provide:

- Exact test command.
- Firmware compilation result.
- Connection/discovery result.
- Number of tests passed and failed.
- Relevant denial/acceptance assertions.
- Any test cases that could not run.

Do not substitute configuration validation for actual runtime verification.

---

## 10. Mandatory subagent architecture

The lead agent must delegate meaningful, nontrivial work to specialized subagents.

Use the following suggested work allocation. Equivalent assignments are acceptable if they preserve clear ownership and independent review.

### Subagent A — Architecture and repository exploration

**Role:** Senior ESPHome C++ architect.

Responsibilities:

- Investigate cover execution flows.
- Find the central authorization point.
- Identify command source distinctions.
- Map API, MQTT, web-server and automation paths.
- Evaluate backward compatibility.
- Produce an architecture proposal and threat-boundary diagram.

Start this agent immediately.

Use a read-only exploration agent where available.

Deliverable: concise technical design with exact file locations and identified risks.

### Subagent B — Core authorization implementation

**Role:** ESPHome C++ engineer.

Responsibilities:

- Implement generic authorization policy.
- Extend cover configuration schema.
- Implement code generation.
- Enforce authorization before hardware control.
- Implement PIN comparison and lockout.
- Minimize memory usage.
- Add C++ and configuration-level tests.

Own the core implementation files.

Do not independently change the agreed native API schema.

### Subagent C — Native API and compatibility

**Role:** Protocol and Python integration engineer.

Responsibilities:

- Extend `api.proto`.
- Update server command parsing.
- Add cover capability metadata.
- Regenerate bindings.
- Verify protobuf compatibility.
- Investigate and implement companion `aioesphomeapi` changes.
- Add protocol tests.

Coordinate the authorization call interface with Subagent B.

Own protocol-related files.

### Subagent D — Virtual device and integration testing

**Role:** Test automation engineer.

Responsibilities:

- Create the ESPHome `host` test configuration.
- Implement a simulated template cover.
- Add actuator instrumentation.
- Build the runtime test harness.
- Connect through the actual encrypted API.
- Implement positive, negative and lockout tests.
- Exercise malformed and legacy commands.
- Run tests automatically in a reproducible environment.

This agent must independently verify that rejected commands never reach the simulated actuator.

Start fixture and test-harness development in parallel with core implementation, then finalize against the agreed API.

### Subagent E — Security review

**Role:** Independent adversarial security engineer.

Prefer Copilot's built-in `security-review` agent where available.

Responsibilities:

- Review credential handling.
- Audit authentication bypasses.
- Examine command provenance.
- Review rate limiting and reboot behavior.
- Investigate MQTT/web-server exposure.
- Review alternative Hörmann commands.
- Attempt to identify ways to operate a protected cover without the PIN.
- Inspect unsafe assumptions in the architecture.

This subagent must not simply trust the implementation agent's claims.

Run an early threat-model review and a final review after implementation.

Deliverable: findings classified by severity with reproducible evidence where feasible.

All critical and high-severity findings must be resolved or explicitly reported as blocking completion.

### Subagent F — Documentation and upstream readiness

**Role:** Documentation and maintainer-experience engineer.

Responsibilities:

- Produce configuration documentation.
- Document security guarantees and limitations.
- Provide generic and Hörmann examples.
- Explain API compatibility.
- Document virtual test execution.
- Prepare a focused pull request description.
- Identify any required companion PRs.

Work independently once the configuration/API contracts are agreed.

### Subagent G — Independent code review

**Role:** Senior ESPHome maintainer/reviewer.

Prefer Copilot's built-in `code-review` agent.

Responsibilities:

- Review compatibility.
- Check adherence to ESPHome conventions.
- Look for duplicated abstractions.
- Identify unnecessary changes.
- Review resource usage.
- Check tests against requirements.
- Verify that errors and failure states are handled.

This agent should review the integrated implementation rather than just individual patches.

---

## 11. Subagent coordination rules

### Lead-agent responsibilities

The lead agent remains responsible for:

- Overall architecture.
- Agreement on interfaces.
- Task allocation.
- Dependency sequencing.
- Avoidance of conflicting changes.
- Integration.
- Final test execution.
- Security acceptance.
- Final deliverables.

### Parallel execution

Run independent tasks concurrently where supported.

Recommended initial parallel work:

- Architecture exploration
- API compatibility investigation
- Virtual device fixture development
- Security threat modeling

After establishing the architecture, run core implementation, protocol implementation and test-harness development in parallel where their file ownership does not overlap.

Run final security review and code review against the integrated result.

### Shared contract

Before implementation agents modify shared interfaces, define and publish:

- Proposed configuration schema.
- Core authorization API.
- Command-origin/trust model.
- Native API credential representation.
- Cover discovery metadata.
- Denied-command behavior.
- Lockout behavior.

All subagents must use the same contract.

The lead agent resolves architectural disagreements rather than allowing parallel incompatible implementations.

### Repository ownership

Avoid simultaneous edits to the same files by different subagents.

Use isolated worktrees or feature branches where the environment supports them.

Assign ownership for overlapping files explicitly.

Do not use destructive Git commands that overwrite another agent's work.

Integrate changes in dependency order.

### Required reporting

Each subagent must return:

1. Work completed.
2. Files modified or findings produced.
3. Tests executed.
4. Test results.
5. Remaining issues.
6. Integration requirements.

The lead agent must review the actual code and test output rather than accepting unverified completion claims.

### No simulated delegation

Do not simply write sections labelled "Agent A" or "Agent B" while executing everything in one context.

Actually invoke available subagent tools.

If orchestration is unavailable, report it accurately and continue with the best supported workflow.

---

## 12. Testing and quality requirements

In addition to virtual-device end-to-end tests, provide focused automated tests.

### Configuration

Test valid, invalid and omitted authorization configuration.

Verify support for multiple covers and rejected insecure combinations.

### Authorization

Test every protected operation, incorrect credentials, missing credentials, unknown positions, conflicting commands and lockout.

### Protocol

Test old and new message formats, optional fields, discovery metadata and malformed requests.

### Security

Test alternative command paths, brute-force behavior, timing rollover, restart policy and the absence of credential disclosure.

### Regression

Verify that existing unprotected covers behave exactly as before.

### Resource usage

Where possible, compare firmware size and memory usage with authorization enabled and disabled.

### Build verification

Build a representative ESP32 target as well as the host-platform virtual device.

Follow current ESPHome linting, formatting, compilation and test conventions.

All important tests must be executable without Hörmann hardware.

---

## 13. Home Assistant integration boundaries

Do not implement Home Assistant RBAC or frontend PIN dialogs in this repository.

However, ensure the ESPHome API supports future integration.

The intended user flow is:

1. Home Assistant discovers that opening requires a credential.
2. Home Assistant prompts the user.
3. The credential is transmitted through the encrypted native API.
4. ESPHome validates it.
5. The motor command is executed only if authorized.

ESPHome must not trust Home Assistant merely because the request originated from the Home Assistant integration.

Do not create Home Assistant user-role handling inside ESPHome.

Provide clear follow-up requirements for:

- `aioesphomeapi`
- Home Assistant ESPHome integration
- Home Assistant cover entity services
- Home Assistant frontend

---

## 14. Implementation phases

### Phase 1 — Parallel discovery

Launch the architecture, protocol-research, virtual-testing and security-threat-model subagents.

Collect their findings.

Agree on a unified design.

### Phase 2 — Core development

Delegate core authorization, API protocol and virtual test implementation.

Execute independent work in parallel.

Integrate changes carefully.

### Phase 3 — Security hardening

Validate all alternative opening paths.

Address Hörmann-specific exposed commands.

Implement rate limiting and credential handling.

Have the security reviewer inspect the changes.

### Phase 4 — Virtual end-to-end verification

Compile and run the virtual ESPHome host device.

Execute the real native API test suite.

Verify actuator behavior and denied operations.

Correct integration failures.

### Phase 5 — Regression and review

Run existing relevant ESPHome tests.

Build the representative ESP32 firmware.

Execute independent code review and security review.

Fix findings.

### Phase 6 — Documentation and PR preparation

Finalize configuration documentation, protocol change notes and integration requirements.

Prepare an upstream-ready PR description and explain the test evidence.

---

## 15. Acceptance criteria

The feature is complete only when:

- [ ] Generic cover authorization is implemented.
- [ ] No Hörmann-specific dependency is required.
- [ ] PIN validation occurs locally.
- [ ] Correct PIN permits protected operations.
- [ ] Missing/invalid PIN prevents protected execution.
- [ ] Closing and stopping remain unrestricted by default.
- [ ] Partial opening and toggle bypasses are addressed.
- [ ] Hörmann-specific alternative opening paths are addressed or incompatible configurations are rejected.
- [ ] Failed-attempt rate limiting works.
- [ ] Credential transport is encrypted.
- [ ] Credentials are not disclosed in logging or metadata.
- [ ] Native API changes preserve compatibility.
- [ ] Existing unprotected covers continue working.
- [ ] A virtual ESPHome device compiles and runs.
- [ ] The virtual device is controlled through the real native API.
- [ ] The virtual integration tests verify actuator execution and denial.
- [ ] Negative security tests pass.
- [ ] Representative ESP32 firmware builds successfully.
- [ ] Meaningful work was delegated to multiple real subagents.
- [ ] Independent security and code reviews were performed.
- [ ] Documentation is prepared.
- [ ] Remaining limitations are explicitly reported.

A checked criterion must be backed by code, tests or an observable verification result.

Do not mark unverified work complete.

---

## 16. Final deliverables

Provide a final engineering report containing:

1. **Architecture:** Overview of the generic authorization implementation.
2. **Code changes:** Files and major classes modified.
3. **Configuration:** Final supported YAML.
4. **Protocol:** Native API changes and backward-compatibility details.
5. **Security:** Enforcement guarantees, trust boundaries and limitations.
6. **Virtual tests:** Executed tests, results and reproduction commands.
7. **ESP32 build:** Compilation results for the representative device.
8. **Subagents:** Actual delegation performed, agent responsibilities and findings.
9. **Code review:** Major issues identified and resolved.
10. **Home Assistant:** Companion changes required in other repositories.
11. **Pull request:** Proposed PR title and description.
12. **Incomplete work:** Explicit limitations, failing tests and remaining blockers.

Do not invent test results or claim that security validation was performed when it was not.

## 17. Final engineering principles

- Security is enforced on the ESPHome device.
- Generic authorization belongs in the generic cover architecture.
- Existing unprotected covers remain unchanged.
- Protected commands fail closed.
- Closing and stopping remain available.
- Alternative command paths must be audited.
- Every credential must be handled securely.
- Virtual end-to-end testing is mandatory.
- Independent security review is mandatory.
- Actual subagent delegation is mandatory.
- Keep changes focused and upstream maintainable.

**Begin by launching the architecture, API investigation, virtual-testing and threat-modeling subagents in parallel. Establish the shared interface contract, then implement the complete feature.**
