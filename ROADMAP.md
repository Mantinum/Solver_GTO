# Roadmap du Développement du Solver GTO

Ce document décrit les étapes de développement prévues pour le solver GTO de poker.

## Phase 1 : Recherche, Conception et Architecture de Base (Terminée)

*   [x] Évaluation de la faisabilité et des algorithmes (CFR+ initialement, puis MCCFR).
*   [x] Choix des technologies (C++17/20, CMake, GoogleTest).
*   [x] Définition de l'architecture modulaire (hand\_generator, hand\_evaluator, action\_abstraction, cfr\_engine).
*   [x] Création de la structure de base du projet (répertoires, CMakeLists.txt).
*   [x] Création des fichiers squelettes pour chaque module (.h et .cpp).
*   [x] Implémentation de base et tests unitaires pour `hand_generator`.
*   [x] Implémentation de base et tests unitaires pour `hand_evaluator` (avec `phevaluator`).
*   [x] Implémentation de base et tests unitaires pour `action_abstraction`.
*   [x] Définition des structures de données (`GameState`, `InfoSet`, `Node`).
*   [x] Mise en place de la structure de la classe `CFREngine`.
*   [x] Mise en place de `main.cpp` pour initialiser et lancer l'entraînement (paramétrable).
*   [x] Compilation et exécution réussies du squelette.

## Phase 2 : Prototype Fonctionnel Préflop MCCFR (Terminée - Base Fonctionnelle)

*   **Objectif :** Obtenir un solver capable d'exécuter des itérations MCCFR sur des scénarios préflop (HU, 6-max), d'extraire la stratégie RFI résultante, d'utiliser le multithreading et le checkpointing.
*   **Étapes Clés Réalisées :**
    *   [x] Implémentation `GameState` (logique multi-way de base, blinds/antes, fin tour/street).
    *   [x] Implémentation `HandEvaluator` (avec `phevaluator`).
    *   [x] Implémentation `ActionAbstraction` (contextuelle, sizings multiples "enrichis").
    *   [x] Implémentation `CFREngine` (calcul payoffs ChipEV multi-way/pots annexes).
    *   [x] Intégration Logging (`spdlog`) et Tests Unitaires (`gtest`) de base.
    *   [x] Implémentation Multithreading.
    *   [x] Implémentation Checkpointing Binaire.
    *   [x] Implémentation Extraction Stratégie RFI (affichage grille + export JSON).
    *   [x] Refactorisation pour utiliser **MCCFR (External Sampling)**.
    *   [x] Stockage des actions légales dans `Node`.
    *   [x] Validation : Exécution MCCFR multithread réussie localement (HU/6-max) sans crash mémoire initial. Profondeur augmente avec itérations.

## Phase 3 : Correction des Problèmes Fondamentaux & Fiabilisation (En Cours - Prioritaire)

*   **Objectif :** Corriger les bugs critiques identifiés par l'audit pour obtenir des stratégies GTO (ChipEV) cohérentes et fiables, même si non parfaitement convergées.
*   **Basé sur l'Audit du 2025-04-23 :**
    *   **🚨 P1 : Élargir la Clé d'InfoSet (`InfoSet::get_key`) :**
        *   [ ] **Inclure la Street :** Ajouter `current_state.get_current_street()` à la clé.
        *   [ ] **Inclure le Board :** Ajouter les cartes communes (triées, avec placeholder si non révélées) à la clé.
        *   [ ] **(Optionnel/Discuter) Inclure Contexte Abstrait :** Ajouter une représentation abstraite du pot/stack/montant à payer pour différencier les spots avec même historique mais contexte différent. Nécessite une fonction de "bucketing".
        *   [ ] Modifier `cfr_engine.cpp` pour passer les informations nécessaires à `get_key`.
    *   **🚨 P1 : Corriger la Pondération MCCFR External Sampling (`cfr_engine.cpp`) :**
        *   [ ] Dans `cfr_plus_recursive`, quand `current_player != traversing_player`, calculer le poids `w = 1.0 / current_strategy[sampled_action_idx]` (en gérant la division par zéro).
        *   [ ] Appliquer ce poids `w` lors de la mise à jour des `regret_sum` et `strategy_sum` du **joueur traversant** (dans le bloc `if (current_player == traversing_player)`), en multipliant les termes `counterfactual_reach_prob * regret` et `player_reach_prob * current_strategy[i]` par `w`. *(Correction: Le poids s'applique aux regrets/stratégies du joueur dont c'est le tour, pondéré par les probabilités d'atteinte des adversaires)*. **À revérifier précisément selon la formulation External Sampling.** La mise à jour des regrets du joueur traversant (`current_player == traversing_player`) utilise `action_utilities[i] - node_utility`. L'utilité `action_utilities[i]` retournée par l'appel récursif pour l'action `i` doit être pondérée par `1/sampling_prob` si l'action `i` a été prise par un adversaire dans cette branche. C'est plus subtil. **Recherche nécessaire sur la bonne application du poids.**
    *   **⚠️ P2 : Revoir/Corriger l'Abstraction d'Action (`action_abstraction.cpp`) :**
        *   [ ] **Conditionner/Supprimer `all_in` préflop** pour stacks > ~40bb dans les scénarios RFI et potentiellement vs RFI.
        *   [ ] Ajouter un sizing de 4Bet non All-in (ex: 2.5x). *(Déjà fait dans `8c1cb33`)*.
        *   [ ] Ajouter des sizings postflop (ex: 25% pot, Overbet 133%, petit raise ~1.8x-2.2x).
        *   [x] **(B-01 Corrigé)** `get_action_amount` ne rejette plus les open-raises quand `amount_to_call == 0`.
        *   [x] **(B-02 Corrigé)** L'affichage de la grille dans `main.cpp` marque correctement "R" pour les actions "bet" et "open".
        *   [ ] **(Plus tard) Refactoriser** pour utiliser `enum class ActionType` + `struct ActionSpec` au lieu de strings.
    *   **⚠️ P2 : Incohérence `legal_actions` ↔ Node :**
        *   [ ] **Solution à court terme :** Dans `cfr_plus_recursive`, *après* avoir trouvé/créé le nœud, **vérifier** si `node_ptr->legal_actions` correspond à `legal_actions_str` calculé actuellement. Si différent, logger un warning critique et potentiellement arrêter/ignorer ce chemin, car la stratégie stockée n'est pas valide pour les actions actuelles.
        *   [ ] **Solution à long terme :** Inclure le contexte abstrait (pot/stack) dans la clé d'InfoSet.
    *   **⚠️ P2 : Règles Min-Raise (`game_state.cpp`) :**
        *   [ ] Revoir la logique de calcul de `min_raise_increment` et `min_legal_total_bet` dans `get_action_amount` et `apply_action` pour gérer tous les cas (préflop BB, vs limps, postflop, all-in courts). Ajouter des tests unitaires spécifiques.
    *   **⚠️ P2 : Thread-Safety `phevaluator` :**
        *   [ ] Vérifier la documentation de `phevaluator` sur la ré-entrance.
        *   [ ] Si non garantie, ajouter un `std::mutex` global autour des appels à `hand_evaluator_.evaluate_7_card_hand` dans `cfr_engine.cpp`.
    *   **ℹ️ P3 : Corriger les Optimisations/Propreté :**
        *   [ ] Optimiser `HandGenerator`.
        *   [ ] Optimiser `MonteCarlo` (si utilisé).
        *   [ ] Factoriser `create_action_string` (utilisé dans `main.cpp` et `action_abstraction.cpp`).
        *   [ ] Factoriser `get_strategy_from_regrets` (utilisé dans `cfr_engine.cpp` et `cfr_engine_test.cpp`).

## Phase 4 : Validation et Entraînement Long (Après Corrections P1/P2)

*   [ ] **Relancer un Run Court de Validation :** Après les corrections P1/P2, lancer 100k-1M itérations pour vérifier :
    *   Différenciation des ranges RFI par position.
    *   Disparition des All-in préflop 100bb anormaux.
    *   Cohérence générale des stratégies émergentes.
    *   Vitesse d'entraînement (devrait être plus lente mais stable).
*   [ ] **Implémenter Mesure d'Exploitabilité :** Ajouter une fonction pour calculer l'exploitabilité de la stratégie moyenne (nécessite de parcourir l'arbre ou d'utiliser des techniques d'approximation).
*   [ ] **Entraînement Long (AWS) :** Lancer l'entraînement final sur AWS avec un grand nombre d'itérations, en surveillant l'exploitabilité pour déterminer la convergence.
*   [ ] **Affiner l'Abstraction d'Action (Itératif) :** Basé sur l'analyse des solutions convergentes, ajuster les sizings.
*   [ ] **Optimiser la Vitesse (si nécessaire) :** Envisager `unordered_map`, etc.
*   [ ] **Améliorer l'Extraction/Visualisation :** Exporter plus de spots, formats CSV/JSON améliorés.

## Phase 5 : Adaptation pour MTT (Future)

*   [ ] Intégration ICM.
*   [ ] Gestion Stacks/Joueurs/Antes Variables.
*   [ ] Abstraction Postflop (Clustering).
*   [ ] Optimisations MTT (FGS).

## Phase 6 : Optimisations Avancées et Deep Learning (Optionnelle/Future)

*   [ ] Deep CFR.

## Phase 7 : Déploiement et Production (Future)

*   [ ] UI, Monitoring, CI/CD.
