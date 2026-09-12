# Proposal — Indoor Navigation System Application

> Team **Pathfinder** · Project **InNavi** · CSD3401 / UXG3400 (Fall 2026)

---

## Overview

Our application is an indoor navigation system designed to help users, such as
students, navigate large buildings and locate specific rooms. For example, a user
could navigate from Level 1 to classroom E2-08-14 on Level 8.

The application will be developed as a **standalone Unreal Engine runtime
application**, using a simplified interior representation of a building to provide
navigation between rooms and floors. As development progresses, we aim to transition
the system towards an **augmented reality (AR)** experience, allowing users to
navigate the building while viewing directions and navigation information within their
real-world surroundings.

## Goal

The main goal is to develop both:

- A **custom building editor/tool** for creating and managing building interiors and
  navigation data.
- A **navigation application** that allows users to explore the generated building and
  navigate to their destination.

The system will be designed to be **dynamic and reusable**, rather than being limited
to the SIT campus. The same editor should be able to support different buildings by
using building-specific interior data.

## System Flow

A user can:

1. Select or load a building.
2. Enter and manage its interior data through the editor interface.
3. Save and load the interior data as **JSON**.
4. Have the interior **procedurally generated at runtime** based on the provided data.
5. Navigate through the generated building, including moving between different floors.
6. Select a destination and receive a calculated route to the selected room.

The generated interior will include basic elements such as:

- Walls
- Windows
- Doors
- Corridors
- Stairs
- Elevators

Elevators and other connections between floors will be represented as
**input/output nodes**, allowing the navigation system to understand connections
between different levels.

## Target Platforms

- **Editor:** Desktop
- **Navigation Application:** Mobile

## Interior Fidelity

The interior will initially use a **schematic representation** to prioritise
simplicity, accuracy, and ease of editing. Basic elements such as walls, rooms, doors,
corridors, stairs, and elevators will be represented without detailed decoration.

A separate **Polish/Preview mode** will allow editors to visualise how the completed
environment could look by automatically applying additional visual details and assets
to the schematic layout.

As the project progresses towards AR, the same underlying building and navigation data
can be used to support an AR navigation experience. This allows the editing system to
remain simple while providing a more visually immersive experience for the end user.

## Navigation

The navigation system will support **multi-floor navigation** through abstract
connectors such as stairs and elevators.

The system will generate a route from the user's current location to their selected
destination. For example:

> Level 1 → Lobby → Elevator → Level 8 → E2-08-14

The navigation will be generated at runtime based on the procedurally generated
interior and its available connections between floors.
