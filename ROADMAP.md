Raise first in# Roadmap du Développement du Solver GTO

Ce document décrit les étapes de développement prévues pour le solver GTO de poker.

## Phase 1 : Recherche, Conception et Architecture de Base (Terminée)

*   [x] Évaluation de la faisabilité et des algorithmes (CFR+ initialement, puis MCCFR).
*   [x] Choix des technologies (C++17/20, CMake, GoogleTest). // C++17 pour std::filesystem
*   [x] Définition de l'architecture modulaire (hand\_generator, hand\_evaluator, action\_abstraction, cfr\_engine). // monte_carlo non utilisé pour l'instant
*   [x] Création de la structure de base du projet (répertoires, CMakeLists.txt).
*   [x] Création des fichiers squelettes pour chaque module (.h et .cpp).
*   [x] Implémentation de base et tests unitaires pour `hand_generator`.
*   [x] Implémentation de base (placeholder) et tests unitaires pour `hand_evaluator`.
*   [x] Implémentation de base (placeholder) et tests unitaires pour `action_abstraction`.
*   [x] Définition des structures de données (`GameState`, `InfoSet`, `Node`).
*   [x] Mise en place de la structure de la classe `CFREngine` avec ses dépendances.
*   [x] Mise en place de `main.cpp` pour initialiser et lancer l'entraînement (paramétrable).
*   [x] Compilation et exécution réussies du squelette.

## Phase 2 : Prototype Fonctionnel Préflop (Terminée - Base MCCFR)

*   **Objectif :** Obtenir un solver capable d'exécuter des itérations **MCCFR** sur des scénarios préflop (HU, 6-max), d'extraire la stratégie résultante, d'utiliser le multithreading et le checkpointing.

*   **Étapes Clés Réalisées :**
    *   [x] **Implémenter `GameState` :** Logique HU et multi-way de base, gestion des blinds/antes, fin de tour/street.
    *   [x] **Implémenter `HandEvaluator` :** Utilisation de `phevaluator`.
    *   [x] **Implémenter `ActionAbstraction` :** Abstraction contextuelle (RFI, vs Limp, vs Raise, Postflop) avec plusieurs sizings (version "enrichie" actuelle).
    *   [x] **Implémenter `CFREngine` (initialement CFR+) :** Calcul des payoffs multi-way/pots annexes (ChipEV).
    *   [x] **Intégrer Logging (`spdlog`) et Tests Unitaires (`gtest`) de base.**
    *   [x] **Implémenter Multithreading** dans `CFREngine::train`.
    *   [x] **Implémenter Checkpointing Binaire** (sauvegarde/chargement de `NodeMap`).
    *   [x] **Implémenter l'Extraction de Stratégie RFI Multi-Positions** dans `main.cpp` (avec affichage grille).
    *   [x] **Refactoriser `CFREngine` pour utiliser MCCFR (External Sampling)** pour résoudre les problèmes de mémoire en 6-max.
    *   [x] **Validation :** Exécution MCCFR multithread réussie en local (HU et 6-max) pour un faible nombre d'itérations. La profondeur d'arbre augmente avec les itérations.

*   **Problèmes Connus / TODOs Immédiats :**
    *   [ ] **Vitesse MCCFR 6-max :** Bien que fonctionnel, la vitesse (~1000 it/s localement) pourrait être améliorée ou indiquer une exploration encore limitée. Nécessite un entraînement long pour évaluer la convergence réelle.
    *   [ ] **Affichage '?' (Mismatch) :** L'affichage de la grille RFI montre des '?' car les nœuds peuvent être créés via des chemins non-RFI avec un nombre d'actions différent. Solution idéale : stocker les actions avec le nœud.
    *   [ ] **Validation Fonctionnelle Approfondie :** Ajouter des tests unitaires spécifiques pour MCCFR, le checkpointing, et les scénarios multi-way complexes.
    *   [ ] **Mesure de Convergence/Exploitabilité :** Essentiel pour valider la qualité des solutions GTO après un entraînement long.

## Phase 3 : Améliorations et Préparation Déploiement (En cours)
*   **Objectif :** Obtenir des solutions préflop ChipEV fiables et exploitables, prêtes pour une utilisation intensive.
*   **Étapes :**
    *   [ ] **Entraînement Long (AWS) :** Lancer des entraînements 6-max et HU avec des millions/milliards d'itérations sur AWS en utilisant MCCFR et le checkpointing.
    *   [ ] **Analyse de Convergence :** Utiliser les métriques d'exploitabilité (à implémenter) pour déterminer quand arrêter l'entraînement.
    *   [ ] **Affiner l'Abstraction d'Action :** Basé sur l'analyse des premières solutions longues, ajuster/ajouter/supprimer des sizings dans `ActionAbstraction` pour améliorer le réalisme et la performance. Itérer entraînement/analyse.
    *   [ ] **Optimiser la Vitesse (si nécessaire) :** Si la vitesse sur AWS reste un frein majeur, envisager `std::unordered_map` pour `NodeMap` ou d'autres optimisations C++.
    *   [ ] **Améliorer l'Extraction/Visualisation :**
        *   [ ] Résoudre le problème des '?' (stocker actions dans Node ou autre méthode).
        *   [ ] Permettre l'extraction d'autres spots (vs Limp, 3Bet, 4Bet, défense BB/SB...).
        *   [ ] Exporter les ranges en format standard (JSON, CSV).

## Phase 4 : Adaptation pour MTT (Future)

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
