# Planarity
The popular puzzle game [Planarity](https://en.wikipedia.org/wiki/Planarity) implemented on the Mbed platform.

This application/project was developed as a part of the undergraduate elective course "Embedded Systems", offered by the Faculty of Electrical Engineering, University of Sarajevo.

The goal of the project was to develop a version of the popular puzzle game Planarity for the Mbed platform. Considering the project was done in 2021, due to COVID-19 restrictions, the project was tested on the Arm Mbed OS simulator. In the simulator the ST7789H2 LCD + FT6x06 Touch Screen combo was used.

The developed game offers three singleplayer playing modes: Classic, Race against time & Crazy. The first of those is the classic Planarity game. Race against time has the same core mechanics but sets a time limit for solving the puzzle. The Crazy game mode adds a twist by randomly moving graph points while the puzzle is being solved. Additionally, a leaderboard system, difficulty settings and themes are also present. A multiplayer game mode is also implemented using MQTT, allowing two players to play against one another.

A demonstration of the game is given in a [YouTube video](https://www.youtube.com/watch?v=SDePe63_CUc).

The repository only contains the source code and is only used for presentation purposes.

Project done by:
- [Ahmed Imamović](https://github.com/aimamovic6)
- [Dženan Kreho](https://github.com/dkreho1)