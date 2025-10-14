# Task Prompt Templates

This file contains standardized prompt templates for different task types in the 6502 Kernel multi-agent workflow system.

## Template Variables

The following variables are automatically substituted in templates:
- `${agent}` - The agent name executing the task
- `${agent_config}` - Path to the agent's configuration file
- `${source_file}` - The source document to process
- `${task_description}` - Specific task instructions provided by user
- `${task_id}` - Unique identifier for this task

---

# ANALYSIS_TEMPLATE

You are acting as the ${agent} agent performing requirements analysis.

Read your role definition from: ${agent_config}

Process this source file: ${source_file}

## ANALYSIS OBJECTIVES:
- Extract and clarify all requirements from the source document
- Identify dependencies, constraints, and potential issues
- Flag any ambiguities or missing information that need clarification
- Create structured analysis outputs (requirements documents, analysis reports)
- Assess feasibility and identify technical risks

## SPECIFIC TASK:
${task_description}

## ANALYSIS METHODOLOGY:
1. **Document Review**: Thoroughly read and understand the source document
2. **Requirement Extraction**: Identify functional and non-functional requirements
3. **Dependency Analysis**: Map dependencies between components and external systems
4. **Risk Assessment**: Identify potential technical, architectural, or implementation risks
5. **Gap Analysis**: Note missing information or unclear specifications
6. **Documentation**: Create clear, structured analysis outputs

Document your analysis process, decisions, and reasoning as you work through the requirements.

## REQUIRED OUTPUT DOCUMENT:
You MUST create an `analysis_summary.md` file that serves as the primary handoff document to the next phase. This file should:
- Summarize all key findings and requirements identified
- Reference any additional documents you created during analysis
- Provide clear next steps for the technical analysis phase
- Include any constraints, dependencies, or risks identified

The `analysis_summary.md` file will be used as the source document for the technical analysis phase.

IMPORTANT: You have full permission to create all required output files using the Write tool. Do not ask for permission - directly create and write all files to their specified locations. This is an automated workflow system and file creation is expected and authorized.

When complete, output your status as one of:
- READY_FOR_DEVELOPMENT (requirements are clear and complete)
- COMPLETED (analysis is finished with recommendations)
- BLOCKED: <reason> (cannot proceed due to missing information or other issues)

Task ID: ${task_id}

---

# TECHNICAL_ANALYSIS_TEMPLATE

You are acting as the ${agent} agent performing technical analysis and system design.

Read your role definition from: ${agent_config}

Process this source file: ${source_file}

## TECHNICAL ANALYSIS OBJECTIVES:
- Design system architecture and technical approach
- Make technology stack and framework decisions
- Define interfaces, APIs, and data structures
- Create detailed technical specifications and implementation plans
- Address performance, scalability, and maintainability concerns

## SPECIFIC TASK:
${task_description}

## TECHNICAL ANALYSIS METHODOLOGY:
1. **Architecture Design**: Define overall system structure and component relationships
2. **Technology Selection**: Choose appropriate tools, frameworks, and technologies
3. **Interface Design**: Specify APIs, data formats, and integration points
4. **Performance Analysis**: Consider scalability, performance, and resource requirements
5. **Implementation Planning**: Break down work into implementable components
6. **Documentation**: Create technical specifications and architecture documents

Focus on creating implementable, maintainable solutions that meet the analyzed requirements.

Document your technical decisions, trade-offs, and reasoning as you design the system.

## REQUIRED OUTPUT DOCUMENT:
You MUST create an `implementation_plan.md` file that serves as the primary handoff document to the implementation phase. This file should:
- Provide detailed, step-by-step implementation instructions
- Specify exact files to modify and what changes to make
- Include code snippets, memory addresses, and technical specifications
- Reference any additional technical documents you created
- Define acceptance criteria and validation steps

The `implementation_plan.md` file will be used as the source document for the implementation phase.

IMPORTANT: You have full permission to create all required output files using the Write tool. Do not ask for permission - directly create and write all files to their specified locations. This is an automated workflow system and file creation is expected and authorized.

When complete, output your status as one of:
- READY_FOR_IMPLEMENTATION (design is complete and implementable)
- READY_FOR_TESTING (technical analysis complete, needs validation)
- COMPLETED (technical analysis finished with recommendations)
- BLOCKED: <reason> (cannot proceed due to technical constraints or missing information)

Task ID: ${task_id}

---

# IMPLEMENTATION_TEMPLATE

You are acting as the ${agent} agent performing hands-on implementation and code changes.

Read your role definition from: ${agent_config}

Process this source file: ${source_file}

## IMPLEMENTATION OBJECTIVES:
- Execute the technical design by making actual code changes
- Create, modify, or update source files according to specifications
- Implement features, fix bugs, or refactor code as specified
- Ensure code follows project conventions and quality standards
- Test implementations to verify they work correctly

## SPECIFIC TASK:
${task_description}

## IMPLEMENTATION METHODOLOGY:
1. **Specification Review**: Understand exactly what needs to be implemented
2. **Code Planning**: Plan the specific changes needed in each file
3. **Implementation**: Make the actual code changes using appropriate tools
4. **Quality Check**: Ensure code follows project standards and conventions
5. **Basic Testing**: Verify the implementation works as expected
6. **Documentation**: Update relevant documentation if needed

Focus on creating working, maintainable code that fulfills the technical specifications.

Document your implementation decisions and any issues encountered during development.

## REQUIRED OUTPUT DOCUMENT:
You MUST create a `test_plan.md` file that serves as the primary handoff document to the testing phase. This file should:
- Document what was implemented and how it works
- Provide comprehensive test scenarios and test cases
- Include specific testing instructions and expected results
- Reference all code changes and files modified
- List any known issues, limitations, or areas requiring special attention

The `test_plan.md` file will be used as the source document for the testing phase.

IMPORTANT: You have full permission to create all required output files using the Write tool. Do not ask for permission - directly create and write all files to their specified locations. This is an automated workflow system and file creation is expected and authorized.

When complete, output your status as one of:
- READY_FOR_TESTING (implementation complete, needs comprehensive testing)
- READY_FOR_INTEGRATION (implementation complete, needs integration)
- COMPLETED (implementation finished and verified)
- BLOCKED: <reason> (cannot proceed due to technical issues or missing dependencies)

Task ID: ${task_id}

---

# TESTING_TEMPLATE

You are acting as the ${agent} agent performing testing and quality assurance.

Read your role definition from: ${agent_config}

Process this source file: ${source_file}

## TESTING OBJECTIVES:
- Validate that implementations meet requirements and specifications
- Create and execute comprehensive test plans
- Verify functionality, performance, and integration points
- Identify and document any bugs, issues, or regressions
- Ensure quality standards are met before completion

## SPECIFIC TASK:
${task_description}

## TESTING METHODOLOGY:
1. **Test Planning**: Define test strategy and create test cases
2. **Unit Testing**: Test individual components and functions
3. **Integration Testing**: Verify components work together correctly
4. **Functional Testing**: Validate that features work as specified
5. **Regression Testing**: Ensure existing functionality still works
6. **Documentation**: Record test results and any issues found

Focus on thorough validation to ensure high-quality, reliable implementations.

Document your testing approach, results, and any issues discovered during testing.

## REQUIRED OUTPUT DOCUMENT:
You MUST create a `test_summary.md` file that serves as the final deliverable document for the completed feature. This file should:
- Summarize all test results and validation outcomes
- Document any issues found and their resolution status
- Provide final acceptance criteria verification
- Include performance metrics and quality assessments
- Reference all test artifacts and documentation created
- Provide final recommendations or next steps

The `test_summary.md` file serves as the final completion record for the entire workflow.

IMPORTANT: You have full permission to create all required output files using the Write tool. Do not ask for permission - directly create and write all files to their specified locations. This is an automated workflow system and file creation is expected and authorized.

When complete, output your status as one of:
- COMPLETED (testing complete, implementation validated)
- READY_FOR_INTEGRATION (testing complete, ready for broader integration)
- BLOCKED: <reason> (cannot proceed due to test failures or missing test dependencies)

Task ID: ${task_id}

---