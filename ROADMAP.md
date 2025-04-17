# Roadmap du Développement du Solver GTO

Ce document décrit les étapes de développement prévues pour le solver GTO de poker.

## Phase 1 : Recherche, Conception et Architecture de Base (Terminée)

*   [x] Évaluation de la faisabilité et des algorithmes (CFR+ choisi pour commencer).
*   [x] Choix des technologies (C++20, CMake, GoogleTest).
*   [x] Définition de l'architecture modulaire (hand\_generator, hand\_evaluator, action\_abstraction, cfr\_engine, monte\_carlo).
*   [x] Création de la structure de base du projet (répertoires, CMakeLists.txt).
*   [x] Création des fichiers squelettes pour chaque module (.h et .cpp).
*   [x] Implémentation de base et tests unitaires pour `hand_generator`.
*   [x] Implémentation de base (placeholder) et tests unitaires pour `hand_evaluator`.
*   [x] Implémentation de base (placeholder) et tests unitaires pour `action_abstraction`.
*   [x] Définition des structures de données (`GameState`, `InfoSet`, `Node`).
*   [x] Mise en place de la structure de la classe `CFREngine` avec ses dépendances.
*   [x] Mise en place de `main.cpp` pour initialiser et lancer l'entraînement (basique).
*   [x] Compilation et exécution réussies du squelette.

## Phase 2 : Prototype Fonctionnel Préflop (En cours)

*   **Objectif :** Obtenir un solver capable d'exécuter des itérations CFR+ sur un scénario préflop simple (ex: Heads-Up), d'extraire la stratégie résultante et d'utiliser le multithreading pour l'entraînement.

*   **Étapes Clés Réalisées :**
    *   [x] **Implémenter `GameState::apply_action` :** Logique de base implémentée. Logique de fin de tour améliorée (utilise `player_acted_this_sequence_` pour multi-way). `update_next_player` saute les joueurs inactifs. `advance_to_next_street` détermine le premier joueur postflop via bouton.
    *   [x] **Implémenter le calcul des payoffs dans `CFREngine::cfr_plus_recursive` :** Cas du fold géré. Logique de base pour showdown multi-way et gestion des pots annexes implémentée.
    *   [x] **Gérer la distribution des mains initiales :** Fait dans `CFREngine::train`.
    *   [x] **Convertir les actions (string) en `Action` struct dans `CFREngine::cfr_plus_recursive` :** Fait.
    *   [x] **Implémenter `ActionAbstraction::get_possible_actions` de manière dynamique :** Logique de base implémentée. Ajout tailles de mise postflop (% pot). Ajout tailles d'open raise préflop (2.2bb, 2.5bb, 3bb). Ajout relances préflop (2.2x, 2.5x, 3x). Ajout relances postflop (pot, 2.0x, 2.5x). Calcul min-raise amélioré. Filtrage actions redondantes. Logique de relance `_Nx` gère les 4-bet+.
    *   [x] **Gestion des blinds et antes :** Logique ajoutée dans `GameState` et paramétrable via `main.cpp`.
    *   [x] **Améliorer `HandEvaluator` :** Interface modifiée (`evaluate_preflop_hand`, `evaluate_7_card_hand`). Implémentation `evaluate_preflop_hand` améliorée. `evaluate_7_card_hand` utilise `PokerHandEvaluator`.
    *   [x] **Implémenter `MonteCarlo::estimate_equity` (basique) :** Simulation contre main aléatoire implémentée.
    *   [x] **Ajouter des tests unitaires pour `cfr_engine` et `GameState` :** Tests ajoutés pour `GameState`. Tests ajoutés pour `CFREngine` (smoke test, regret matching, StrategySumToOne).
    *   [x] **Intégrer le Logging (`spdlog`) :** Fait.
    *   [x] **Améliorer la logique de fin de tour/street** dans `GameState` (utilisation état interne).
    *   [x] **Améliorer le calcul des tailles de raise/bet** dans `ActionAbstraction`.
    *   [x] **Implémenter l'extraction de stratégie préflop** dans `main.cpp`.
    *   [x] **Implémenter le multithreading** dans `CFREngine::train` pour accélérer l'entraînement.
    *   [x] **Validation :** Exécution multithread réussie. Stratégie préflop extraite (non convergente avec peu d'itérations, mais le mécanisme fonctionne).

*   **TODOs / Améliorations Possibles pour cette Phase :**
    *   [ ] **Validation Multi-way Approfondie :** Ajouter des tests spécifiques pour les scénarios multi-way complexes et les pots annexes.
    *   [ ] **Affiner/Ajouter Tailles de Mise :** Revoir les tailles de mise dans `ActionAbstraction` si nécessaire pour plus de réalisme ou de granularité.
    *   [ ] **Mesure de Convergence/Exploitabilité :** Implémenter une métrique (ex: calcul d'exploitabilité) pour évaluer la qualité de la stratégie GTO après un entraînement long.
    *   [ ] **Améliorer l'Affichage des Ranges :** Formater la sortie de `main.cpp` en grille de range ou exporter en CSV/JSON.
    *   [ ] **Étendre `MonteCarlo::estimate_equity` :** Gérer les ranges d'équité au lieu d'une seule main aléatoire (utile pour certaines variantes de CFR ou analyses post-calcul).

## Phase 3 : Adaptation pour MTT et Enrichissement (Future)

*   [ ] **Intégration ICM (Independent Chip Model) :** Modifier le calcul des payoffs dans `CFREngine` pour utiliser l'équité du tournoi ($ICM) au lieu des jetons bruts. C'est l'étape **fondamentale** pour les MTT. Nécessite une fonction d'évaluation ICM.
*   [ ] **Gestion des Stacks Variables :** Adapter `GameState` et potentiellement `InfoSet` pour prendre en compte les tailles de tapis effectives variables (ou résoudre pour des profondeurs spécifiques).
*   [ ] **Gestion du Nombre de Joueurs Variable :** Adapter la logique pour gérer différents nombres de joueurs à la table (ou résoudre pour des formats spécifiques : 9-max, 6-max, HU).
*   [ ] **Gestion des Antes Évolutives :** Assurer que la structure des blinds/antes peut être facilement modifiée ou prise en compte.
*   [ ] **Abstraction Postflop (si nécessaire) :** Si l'objectif inclut le jeu postflop en MTT, implémenter l'abstraction de board (clustering) et potentiellement MCCFR.
*   [ ] **Optimisations Spécifiques MTT :** Explorer des techniques comme le "Future Game Simulation" (FGS) pour mieux approximer l'ICM futur.

## Phase 4 : Optimisations Avancées et Deep Learning (Optionnelle/Future)

*   [ ] Explorer l'utilisation de réseaux neuronaux (Deep CFR).
*   [ ] Mettre en place le pipeline d'entraînement.

## Phase 5 : Déploiement et Production (Future)

*   [ ] Développer une interface utilisateur.
*   [ ] Mettre en place la sauvegarde/reprise (checkpoints).
*   [ ] Intégrer le monitoring avancé.
*   [ ] Conteneurisation (Docker) et CI/CD.
